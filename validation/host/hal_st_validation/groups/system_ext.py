"""System groups of validation/PROTOCOL.md: `flash` (`hal::FlashInternalStm`/`FlashHomogeneousInternalStm`,
`variant=sync` their `hal::Synchronous*` twins, on STM32WB55 `variant=coord` `hal::FlashCoordinatedWithWirelessStack`)
over the scratch region of the board, `hsem` (STM32WB55: `HAL_HSEM_*`, `hal::SynchronousHardwareSemaphoreStm`), `bkp`
(`hal::BackupRamStm`) and `lpm` (`hal::LowPowerModeStm`). All four are stateless; `flash.stack starting` and
`hsem.take` register their undo with `close_all`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from ad3_waveforms_bench.protocol import Event
from ad3_waveforms_bench.terminal import PendingCommand

from ..patterns import Pattern
from .base import Group, Pin

__all__ = [
    "BKP_FILL_STEP",
    "GROUPS",
    "LAYOUTS",
    "VARIANTS",
    "BackupRam",
    "Flash",
    "FlashInfo",
    "Hsem",
    "HsemStatus",
    "LowPower",
    "LowPowerWake",
    "bkp_fill_value",
    "flash_words",
    "sector_addresses",
    "table_sectors",
]

Variant = Literal["sync", "async", "coord"]
Layout = Literal["homogeneous", "table"]
StackState = Literal["stopped", "starting", "fus"]
Output = Literal["hex", "crc"]
PowerMode = Literal["sleep", "deep"]

VARIANTS: tuple[Variant, ...] = ("sync", "async", "coord")
LAYOUTS: tuple[Layout, ...] = ("homogeneous", "table")

# `layout=table` (FlashGroup.cpp): single pages first, then this page pattern repeated twice, so the multi-page sectors
# keep indices past the image end page.
TABLE_PATTERN = (1, 1, 2, 4)
TABLE_PATTERN_REPEATS = 2
# `bkp.fill`/`bkp.check` (BackupRamGroup.cpp): word i = seed ^ (step * (i + 1)), 32 bits.
BKP_FILL_STEP = 0x9E3779B9
_MASK32 = 0xFFFFFFFF


def table_sectors(pages: int) -> list[int]:
    """Sector sizes in pages of `layout=table` for a region of `pages` pages."""
    pattern = sum(TABLE_PATTERN)
    repeats = min(TABLE_PATTERN_REPEATS, pages // pattern)
    return [1] * (pages - repeats * pattern) + list(TABLE_PATTERN) * repeats


def sector_addresses(sizes: list[int], page: int) -> list[int]:
    """Region-relative start address of every sector, plus the region size as the last entry."""
    addresses = [0]
    for size in sizes:
        addresses.append(addresses[-1] + size * page)
    return addresses


def flash_words(address: int, length: int, word: int) -> range:
    """Indices of the flash words (`word` bytes: 8 on STM32WB55, 16 on STM32WBA55) a write of `length` bytes at
    `address` programs."""
    return range(address // word, (address + length + word - 1) // word)


def bkp_fill_value(seed: int, index: int) -> int:
    return (seed ^ (BKP_FILL_STEP * (index + 1))) & _MASK32


@dataclass(frozen=True)
class FlashInfo:
    base: int
    sectors: int
    size: int
    # The first sector `flash.erase`/`flash.write` accept: an unfixed erase cannot reach the running image from it on.
    first: int
    # The first absolute page past the running image.
    image: int
    layout: str
    raw: str


class Flash(Group):
    """Addresses are relative to the scratch region; sectors are those of the selected layout."""

    prefix = "flash"

    def info(self, variant: Variant | None = None, layout: Layout | None = None) -> FlashInfo:
        response = self._cmd("info", variant=variant, layout=layout)
        return FlashInfo(
            base=response.as_int("base"),
            sectors=response.as_int("sectors"),
            size=response.as_int("size"),
            first=response.as_int("first"),
            image=response.as_int("image"),
            layout=response["layout"],
            raw=response.raw,
        )

    def erase(
        self, first: int, end: int, variant: Variant | None = None, layout: Layout | None = None, cmd_timeout: float | None = None
    ) -> int:
        """Erases sectors `first` up to `end` (exclusive); returns `us`."""
        return self._cmd("erase", first, end, variant=variant, layout=layout, cmd_timeout=cmd_timeout).as_int("us")

    def write(
        self,
        address: int,
        data: bytes | None = None,
        length: int | None = None,
        pattern: Pattern | None = None,
        seed: int | None = None,
        variant: Variant | None = None,
        layout: Layout | None = None,
        cmd_timeout: float | None = None,
    ) -> int:
        """Writes `data`, or `length` bytes the firmware generates (`pattern`, `seed`); returns `us`."""
        return self.begin_write(address, data, length, pattern, seed, variant, layout, cmd_timeout).wait().as_int("us")

    def begin_write(
        self,
        address: int,
        data: bytes | None = None,
        length: int | None = None,
        pattern: Pattern | None = None,
        seed: int | None = None,
        variant: Variant | None = None,
        layout: Layout | None = None,
        cmd_timeout: float | None = None,
    ) -> PendingCommand:
        payload = data if data is not None else b""
        return self.begin(
            "write", address, payload, len=length, pattern=pattern, seed=seed, variant=variant, layout=layout, cmd_timeout=cmd_timeout
        )

    def read(self, address: int, length: int, variant: Variant | None = None, layout: Layout | None = None) -> bytes:
        return self._cmd("read", address, length, variant=variant, layout=layout).as_bytes("data")

    def read_crc(self, address: int, length: int, variant: Variant | None = None, layout: Layout | None = None) -> int:
        """CRC-32 of `length` bytes (`out=crc`, any length within the region)."""
        response = self._cmd("read", address, length, out="crc", variant=variant, layout=layout)
        if response.as_int("len") != length:
            raise AssertionError(f"read {response['len']} bytes instead of {length}: {response.raw}")
        return int(response["crc"], 16)

    def stack(self, state: StackState, layout: Layout | None = None) -> None:
        """STM32WB55: `starting` keeps a coordinated driver whose steps wait until `stopped` or `fus`."""
        self._cmd("stack", state, layout=layout)
        if state == "starting":
            self._fw.track(("flash", "stack"), "flash.stack", "fus")
        else:
            self._fw.untrack(("flash", "stack"))

    def wait_done(self, timeout: float) -> Event:
        """The `EVT flash` of an operation that completed after its `ERR timeout`."""
        return self._fw.terminal.wait_event("flash", timeout=timeout)


@dataclass(frozen=True)
class HsemStatus:
    locked: bool
    core: int
    procid: int


class Hsem(Group):
    """STM32WB55 hardware semaphores 0-31."""

    prefix = "hsem"

    def take(self, semaphore: int, procid: int, hold: int | None = None) -> None:
        """`hold` (ms): an EMIL timer releases it; without, `close_all` does."""
        self._cmd("take", semaphore, procid=procid, hold=hold)
        if hold is None:
            self._fw.track(("hsem", semaphore, procid), "hsem.release", semaphore, f"procid={procid}")

    def release(self, semaphore: int, procid: int) -> None:
        self._cmd("release", semaphore, procid=procid)
        self._fw.untrack(("hsem", semaphore, procid))

    def status(self, semaphore: int) -> HsemStatus:
        response = self._cmd("status", semaphore)
        return HsemStatus(locked=response.as_bool("locked"), core=response.as_int("core"), procid=response.as_int("procid"))

    def lock(self, semaphore: int, hold: int | None = None, cmd_timeout: float | None = None) -> int:
        """Locks through `hal::SynchronousHardwareSemaphoreStm`; with `hold` (us) the scaffold timer holds it that long
        first. Returns `waited` (us)."""
        return self._cmd("lock", semaphore, hold=hold, cmd_timeout=cmd_timeout).as_int("waited")

    def mine(self, semaphore: int) -> bool:
        return self._cmd("mine", semaphore).as_bool("mine")


class BackupRam(Group):
    prefix = "bkp"

    def info(self) -> int:
        return self._cmd("info").as_int("words")

    def write(self, index: int, value: int) -> None:
        self._cmd("write", index, f"0x{value:08x}")

    def read(self, index: int) -> int:
        return int(self._cmd("read", index)["value"], 16)

    def fill(self, seed: int) -> None:
        self._cmd("fill", seed)

    def check(self, seed: int) -> int:
        """Words that differ from `bkp_fill_value(seed, i)`."""
        return self._cmd("check", seed).as_int("mismatches")


@dataclass(frozen=True)
class LowPowerWake:
    woke: str
    restored: int
    us: int
    sleeps: int
    raw: str


class LowPower(Group):
    """`lpm.enter` masks every interrupt but the wake line and the scaffold timer: nothing may be sent to the board
    until its final line."""

    prefix = "lpm"

    def enter(
        self,
        mode: PowerMode,
        wake: Pin | None = None,
        edge: Literal["rising", "falling"] | None = None,
        marker: Pin | None = None,
        timeout: int | None = None,
        cmd_timeout: float | None = None,
    ) -> LowPowerWake:
        return self.begin_enter(mode, wake, edge, marker, timeout, cmd_timeout).wait()

    def begin_enter(
        self,
        mode: PowerMode,
        wake: Pin | None = None,
        edge: Literal["rising", "falling"] | None = None,
        marker: Pin | None = None,
        timeout: int | None = None,
        cmd_timeout: float | None = None,
    ) -> PendingEnter:
        limit = cmd_timeout if cmd_timeout is not None else self._fw.terminal.timeout + (timeout if timeout is not None else 2000) / 1000
        pending = self.begin("enter", mode, wake=self._pin(wake), edge=edge, marker=self._pin(marker), timeout=timeout, cmd_timeout=limit)
        return PendingEnter(pending)


class PendingEnter:
    """An `lpm.enter` whose final line is still to come."""

    def __init__(self, pending: PendingCommand) -> None:
        self.pending = pending

    def wait(self, timeout: float | None = None) -> LowPowerWake:
        response = self.pending.wait(timeout)
        return LowPowerWake(
            woke=response["woke"],
            restored=response.as_int("restored"),
            us=response.as_int("us"),
            sleeps=response.as_int("sleeps"),
            raw=response.raw,
        )


GROUPS: dict[str, type[Group]] = {"flash": Flash, "hsem": Hsem, "bkp": BackupRam, "lpm": LowPower}
