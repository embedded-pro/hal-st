"""Fake `flash`, `hsem`, `bkp` and `lpm` groups: the argument checks of validation/firmware/FlashGroup.cpp,
HsemGroup.cpp, BackupRamGroup.cpp and LowPowerGroup.cpp in their order, and simple models behind them.

- `flash`: the scratch region as a byte array with page erase, both layouts, the double-safe first sector and the
  erased-word check. `variant=coord` (STM32WB55) waits while HSEM 7 is held or `flash.stack starting` holds it; the
  coordinated driver borrows the watchdog (`wdt.start` answers `ERR busy` meanwhile) and holds HSEM 0 against
  `hsem.lock`.
- `hsem`: lock state of the 32 semaphores, `hold=` timers on the fake clock; `hsem.lock hold=` waits the hold.
- `bkp`: words that survive `reset` (the backup domain keeps them).
- `lpm`: wakes at once (`woke=exti`, `sleeps=1`), as if the wake edge came right after the marker went low.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import TYPE_CHECKING, ClassVar, cast

from ..groups.system_ext import LAYOUTS, VARIANTS, bkp_fill_value, table_sectors
from ..patterns import PATTERNS, SEED_MAX, Pattern, crc_text, generate
from .base import UINT32_MAX, FakeGroup, _choice, _fail, _hex, _number, _shape

if TYPE_CHECKING:
    from ..fake_firmware import FakeFirmware

__all__ = ["FakeBackupRam", "FakeFlash", "FakeHsem", "FakeLowPower", "FakeWatchdogShare"]

_PAYLOAD = 512
_HEX_OUTPUT_MAX = 128
_FIRST_PAGE = {"stm32wb55": 64, "stm32wba55": 64, "stm32g474": 176}
_FLASH_BASE = 0x08000000
# (page size, flash word, end page of the firmware image, end page of the scratch region)
_GEOMETRY = {"stm32wb55": (4096, 8, 60, 144), "stm32wba55": (8192, 16, 24, 128), "stm32g474": (2048, 8, 116, 252)}
_ERASE_US_PER_PAGE = {"stm32wb55": 22000, "stm32wba55": 3000, "stm32g474": 22000}
_WRITE_US_PER_WORD = 90
_ERASE_TIMEOUT = 10.0
_TRANSFER_TIMEOUT = 2.0
_FLASH_SEMAPHORE = 7
_STACK_STATES = ("stopped", "starting", "fus")
_FLASH_OWNER = ("flash", "coord")

_SEMAPHORES = 32
_PROCESS_MAX = 255
_HOLD_MAX_MS = 60000
_LOCK_HOLD_MIN_US = 2
_LOCK_HOLD_MAX_US = 65536
_LOCK_HOLD_PROCESS = 1
_CORE_CPU1 = 4
_HSEM_OWNER = ("hsem", "lock")
_SCAFFOLD_TIMER = 17

_BKP_WORDS = {"stm32wb55": 20, "stm32wba55": 16, "stm32g474": 16}

_LPM_DEFAULTS = {"stm32wb55": ("PC6", "PB0"), "stm32wba55": ("PB14", "PA2"), "stm32g474": ("PC4", "PA12")}
_LPM_TIMEOUT_MAX_MS = 10000
_LPM_OWNER = ("lpm", "enter")
_LPM_WAKE_US = 100


def _payload(text: str, options: dict[str, str], capacity: int) -> bytes:
    """`ParsePayload` of validation/firmware/Payload.cpp."""
    generated = "len" in options
    patterned = "pattern" in options or "seed" in options
    if text != "-":
        if generated or patterned:
            _fail("usage")
        return _hex(text, capacity)
    if not generated:
        if patterned:
            _fail("usage")
        return b""
    pattern = _choice(options, "pattern", PATTERNS, "inc")
    seed = _number(options.get("seed", "0"), 0, SEED_MAX)
    length = _number(options["len"], 1, capacity)
    return generate(length, cast(Pattern, pattern), seed)


@dataclass
class _Held:
    """A coordinated erase or write waiting for HSEM 7 or for the end of `flash.stack starting`."""

    job: str
    apply: Callable[[], int]
    started: float
    deadline: float
    timed_out: bool = False


class FakeFlash(FakeGroup):
    prefix = "flash"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        page, self.word, self.image, end_page = _GEOMETRY.get(fw.family, _GEOMETRY["stm32wb55"])
        self.page = page
        self.first_page = _FIRST_PAGE.get(fw.family, _FIRST_PAGE["stm32wb55"])
        self.pages = end_page - self.first_page
        self.memory = bytearray(b"\xff" * (self.pages * page))
        self.table = table_sectors(self.pages)
        self.boot()

    def boot(self) -> None:
        self.held: _Held | None = None
        self.stack_held = False
        self.layout = "homogeneous"

    @property
    def base(self) -> int:
        return _FLASH_BASE + self.first_page * self.page

    def borrowing(self) -> bool:
        """The coordinated driver exists (it holds the watchdog and HSEM 0)."""
        return self.stack_held or self.held is not None

    def _sizes(self, layout: str) -> list[int]:
        return self.table if layout == "table" else [1] * self.pages

    def _address(self, layout: str, sector: int) -> int:
        return sum(self._sizes(layout)[:sector]) * self.page

    def _first(self, layout: str) -> int:
        if not self.fw.spec.image_limits_erase:
            return 0
        return min(self.image, len(self._sizes(layout)))

    def _driver(self, options: dict[str, str]) -> tuple[str, str]:
        variant = _choice(options, "variant", VARIANTS, "sync")
        layout = _choice(options, "layout", LAYOUTS, "homogeneous")
        return variant, layout

    def _check_variant(self, variant: str) -> None:
        if variant == "coord" and self.fw.family != "stm32wb55":
            _fail("unsupported")

    def _semaphore_free(self) -> bool:
        hsem = self.fw.group("hsem")
        return not isinstance(hsem, FakeHsem) or hsem.holder(_FLASH_SEMAPHORE) is None

    def _reserve(self, variant: str, layout: str, synchronous_read: bool = False) -> None:
        """`FlashCommands::Reserve`."""
        if synchronous_read:
            return
        if self.stack_held:
            if self.held is not None or variant != "coord" or layout != self.layout:
                _fail("busy")
            return
        if self.held is not None:
            _fail("busy")

    def _release(self) -> None:
        self.held = None
        self.fw.release_resources(_FLASH_OWNER)

    def _run(self, variant: str, job: str, apply: Callable[[], int], timeout: float) -> str | None:
        if variant != "coord":
            return f"OK us={apply()}"
        self.fw.claim_resource("hsem", 0, _FLASH_OWNER)
        if not self.stack_held and self._semaphore_free():
            us = apply()
            self._release()
            return f"OK us={us}"
        now = self.fw.clock()
        self.held = _Held(job, apply, now, now + timeout)
        return None

    def poll(self) -> None:
        held = self.held
        if held is None:
            return
        if not self.stack_held and self._semaphore_free():
            apply_us = held.apply()
            us = max(apply_us, round((self.fw.clock() - held.started) * 1e6))
            self.held = None
            if not self.stack_held:
                self.fw.release_resources(_FLASH_OWNER)
            self.fw.event(f"EVT flash op={held.job} us={us}" if held.timed_out else f"OK us={us}")
            return
        if not held.timed_out and self.fw.clock() >= held.deadline:
            held.timed_out = True
            self.fw.event("ERR timeout")

    def cmd_info(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("variant", "layout"))
        variant, layout = self._driver(options)
        self._check_variant(variant)
        sectors = len(self._sizes(layout))
        return (
            f"OK base=0x{self.base:08x} sectors={sectors} size={len(self.memory)} first={self._first(layout)} "
            f"image={self.image} layout={layout}"
        )

    def cmd_erase(self, args: list[str], options: dict[str, str]) -> str | None:
        _shape(args, options, 2, 2, ("variant", "layout"))
        begin = _number(args[0])
        end = _number(args[1])
        variant, layout = self._driver(options)
        sectors = len(self._sizes(layout))
        if begin < self._first(layout) or begin >= end or end > sectors:
            _fail("range")
        self._check_variant(variant)
        self._reserve(variant, layout)
        start, stop = self._address(layout, begin), self._address(layout, end)

        def apply() -> int:
            self.memory[start:stop] = b"\xff" * (stop - start)
            return (stop - start) // self.page * _ERASE_US_PER_PAGE.get(self.fw.family, 22000)

        return self._run(variant, "erase", apply, _ERASE_TIMEOUT)

    def cmd_write(self, args: list[str], options: dict[str, str]) -> str | None:
        _shape(args, options, 2, 2, ("len", "pattern", "seed", "variant", "layout"))
        address = _number(args[0])
        variant, layout = self._driver(options)
        if self.held is not None:
            _fail("busy")
        data = _payload(args[1], options, _PAYLOAD)
        if not data:
            _fail("usage")
        if address < self._address(layout, self._first(layout)) or address + len(data) > len(self.memory):
            _fail("range")
        self._check_variant(variant)
        self._reserve(variant, layout)
        first = address // self.word * self.word
        last = min((address + len(data) + self.word - 1) // self.word * self.word, len(self.memory))
        if any(byte != 0xFF for byte in self.memory[first:last]):
            _fail("failed")

        def apply() -> int:
            self.memory[address : address + len(data)] = data
            return (last - first) // self.word * _WRITE_US_PER_WORD

        return self._run(variant, "write", apply, _TRANSFER_TIMEOUT)

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, ("out", "variant", "layout"))
        address = _number(args[0])
        length = _number(args[1], 1, UINT32_MAX)
        output = _choice(options, "out", ("hex", "crc"), "hex")
        variant, layout = self._driver(options)
        if address + length > len(self.memory) or (output == "hex" and length > _HEX_OUTPUT_MAX):
            _fail("range")
        self._check_variant(variant)
        self._reserve(variant, layout, synchronous_read=variant == "sync")
        data = bytes(self.memory[address : address + length])
        if output == "crc":
            return f"OK len={length} crc={crc_text(data)}"
        return f"OK data={data.hex()}"

    def cmd_stack(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("layout",))
        state = args[0]
        if state not in _STACK_STATES:
            _fail("usage")
        layout = _choice(options, "layout", LAYOUTS, "homogeneous")
        if state != "starting":
            if self.stack_held:
                self.stack_held = False
                if self.held is None:
                    self._release()
            return "OK"
        if self.stack_held:
            _fail("busy")
        self._reserve("coord", layout)
        self.fw.claim_resource("hsem", 0, _FLASH_OWNER)
        self.stack_held = True
        self.layout = layout
        return "OK"


class FakeWatchdogShare(FakeGroup):
    """`wdt.start` of WatchDogFactory.cpp while the coordinated flash borrows the unstarted WWDG: `ERR busy` after the
    argument checks; the other `wdt` commands stay with the fake firmware."""

    prefix = "wdt"

    def cmd_start(self, args: list[str], options: dict[str, str]) -> str:
        flash = self.fw.group("flash")
        borrowed = isinstance(flash, FakeFlash) and flash.borrowing() and self.fw.watchdog is None
        result = self.fw._cmd_wdt_start(args, options)
        if borrowed:
            self.fw.watchdog = None
            self.fw.release(("wdt", args[0]))
            _fail("busy")
        return result


class FakeHsem(FakeGroup):
    prefix = "hsem"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        self.boot()

    def boot(self) -> None:
        self.locks: dict[int, int] = {}
        self.hold: tuple[int, int, float] | None = None

    def holder(self, semaphore: int) -> int | None:
        """The process that holds `semaphore`, after the `hold=` timer had its turn."""
        if self.hold is not None and self.fw.clock() >= self.hold[2]:
            held, process, _ = self.hold
            self.hold = None
            if self.locks.get(held) == process:
                del self.locks[held]
        return self.locks.get(semaphore)

    def poll(self) -> None:
        self.holder(0)

    def _semaphore(self, text: str) -> int:
        return _number(text, 0, _SEMAPHORES - 1)

    def _process(self, args: list[str], options: dict[str, str]) -> tuple[int, int]:
        if "procid" not in options:
            _fail("usage")
        return self._semaphore(args[0]), _number(options["procid"], 1, _PROCESS_MAX)

    def cmd_take(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("procid", "hold"))
        semaphore, process = self._process(args, options)
        hold = _number(options["hold"], 1, _HOLD_MAX_MS) if "hold" in options else None
        holder = self.holder(semaphore)
        if hold is not None and self.hold is not None:
            _fail("busy")
        if holder is not None and holder != process:
            _fail("busy")
        self.locks[semaphore] = process
        if hold is not None:
            self.hold = (semaphore, process, self.fw.clock() + hold / 1000)
        return "OK"

    def cmd_release(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("procid",))
        semaphore, process = self._process(args, options)
        if self.holder(semaphore) == process:
            del self.locks[semaphore]
        if self.hold is not None and self.hold[:2] == (semaphore, process):
            self.hold = None
        return "OK"

    def cmd_status(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        process = self.holder(self._semaphore(args[0]))
        if process is None:
            return "OK locked=0 core=0 procid=0"
        return f"OK locked=1 core={_CORE_CPU1} procid={process}"

    def cmd_lock(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("hold",))
        semaphore = self._semaphore(args[0])
        hold = _number(options["hold"], _LOCK_HOLD_MIN_US, _LOCK_HOLD_MAX_US) if "hold" in options else 0
        self.fw.check_resources(_HSEM_OWNER, [("hsem", 0)])
        if hold and self.fw.timer_owners.get(_SCAFFOLD_TIMER, _HSEM_OWNER) != _HSEM_OWNER:
            _fail("busy")
        if self.holder(semaphore) is not None:
            _fail("busy")
        return f"OK waited={hold}"

    def cmd_mine(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        return f"OK mine={int(self.holder(self._semaphore(args[0])) is not None)}"


class FakeBackupRam(FakeGroup):
    prefix = "bkp"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        self.words = [0] * _BKP_WORDS.get(fw.family, 20)

    def _index(self, text: str) -> int:
        return _number(text, 0, len(self.words) - 1)

    def cmd_info(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        return f"OK words={len(self.words)}"

    def cmd_write(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        index = self._index(args[0])
        self.words[index] = _number(args[1])
        return "OK"

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        return f"OK value={self.words[self._index(args[0])]:08x}"

    def cmd_fill(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        seed = _number(args[0])
        self.words = [bkp_fill_value(seed, index) for index in range(len(self.words))]
        return "OK"

    def cmd_check(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        seed = _number(args[0])
        mismatches = sum(1 for index, word in enumerate(self.words) if word != bkp_fill_value(seed, index))
        return f"OK mismatches={mismatches}"


class FakeLowPower(FakeGroup):
    prefix = "lpm"
    owner: ClassVar[tuple[str, str]] = _LPM_OWNER

    def cmd_enter(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("wake", "edge", "marker", "timeout"))
        if args[0] not in ("sleep", "deep"):
            _fail("usage")
        _choice(options, "edge", ("rising", "falling"), "rising")
        if "timeout" in options:
            _number(options["timeout"], 1, _LPM_TIMEOUT_MAX_MS)
        default_wake, default_marker = _LPM_DEFAULTS.get(self.fw.family, _LPM_DEFAULTS["stm32wb55"])
        wake = self.fw.pin(options.get("wake")) or default_wake
        marker = self.fw.pin(options.get("marker")) or default_marker
        if wake == marker:
            _fail("usage")
        if args[0] == "deep" and not self.fw.spec.lpm_deep:
            _fail("unsupported")
        if not self.fw.bonded(wake) or not self.fw.bonded(marker):
            _fail("pin")
        if not self.fw.supports_interrupt(wake):
            _fail("unsupported")
        if self.fw.timer_owners.get(_SCAFFOLD_TIMER, self.owner) != self.owner:
            _fail("busy")
        self.fw.check_pins(self.owner, [wake, marker])
        return f"OK woke=exti restored=0 us={_LPM_WAKE_US} sleeps=1"
