"""Fake `i2c`, `i2cs` and `eeprom` groups: the argument checks of validation/firmware/I2cGroup.cpp, I2cTarget.cpp,
EepromGroup.cpp and EMIL's `HilEepromCommands` in their order, the pins and `ResourceAllocation` i2c instances they
hold, and one bus that every open master, target and attached EEPROM adapter share (the fake does not know the
wiring), with two devices: the `i2cs` target while it is open, and a 24LC256 at 0x50 that keeps its contents across
resets. General calls (address 0) are not acknowledged. Arbitration loss and clock stretching are accepted and not
modelled (they need the AD3); `i2cs.cfg fault=stop` makes the next read answer `buserror`.
"""

from __future__ import annotations

import zlib
from dataclasses import dataclass, field
from typing import Any

from ..groups.i2c import (
    DEFAULT_TIMING,
    EEPROM_BUFFER,
    I2C_BUFFER,
    I2C_HEX_MAX,
    TARGET_DUMP_MAX,
    TARGET_LAST_MAX,
    TARGET_REGISTERS,
    TIMING_MASK,
)
from ..i2c import BUS_MAX, BUS_MIN, expected_timing
from ..patterns import PATTERNS, SEED_MAX, crc_text, generate
from .base import UINT32_MAX, FakeGroup, _choice, _fail, _hex, _number, _shape

_INSTANCES = 4
_ADDRESS_MAX = 0x7F
_TARGET_ADDRESS_MIN = 0x08
_TARGET_ADDRESS_MAX = 0x77
_TARGET_ADDRESS = 0x42
_STRETCH_MAX = 10_000
_POSITION_MAX = 0xFFFF
_TARGET_BUS = 400_000
_EEPROM_SIZE_MAX = 65536
_EEPROM_PAGE_MIN = 8
_EEPROM_PAGE_MAX = 256
_EEPROM_WRITE_CYCLE_MAX = 20
_ONE_BYTE_SPACE = 256
# A page write keeps the chip busy for this many address phases (the adapter's ACK polling sees them NACKed) or
# until the host's next command, whichever comes first.
_WRITE_CYCLE_POLLS = 2
_POLLS_MAX = 1000

_OPEN_KEYS = ("scl", "sda", "freq", "timing", "pull")
_WRITE_KEYS = ("next", "len", "pattern", "seed")
_READ_KEYS = ("next", "out")
_TARGET_KEYS = ("scl", "sda", "addr", "mode", "timing")
_CONFIGURE_KEYS = ("nack", "addrnack", "stretch", "stretchat", "fault", "faultat", "pattern", "seed")
_ATTACH_KEYS = ("scl", "sda", "addr", "size", "page", "abytes", "freq", "wcycle")
_NEXT = ("stop", "restart", "continue")


@dataclass
class _Chip:
    """24LC256: 32 KiB, 64-byte pages, a 16-bit word address. Data bytes of a write land inside the page of the
    address (wrapping at its end) once the STOP arrives, which starts the write cycle; reads continue from the
    address counter and wrap at the end of the memory."""

    address: int = 0x50
    size: int = 32768
    page: int = 64
    memory: bytearray = field(default_factory=lambda: bytearray(b"\xff" * 32768))
    pointer: int = 0
    cycle: int = 0
    received: bytearray = field(default_factory=bytearray)

    def acknowledge(self, read: bool) -> bool:
        if self.cycle:
            self.cycle -= 1
            return False
        self.received = bytearray()
        return True

    def write(self, data: bytes) -> int:
        for byte in data:
            self.received.append(byte)
            if len(self.received) == 2:
                self.pointer = (self.received[0] << 8 | self.received[1]) % self.size
        return len(data)

    def read(self, length: int) -> bytes | None:
        data = bytes(self.memory[(self.pointer + offset) % self.size] for offset in range(length))
        self.pointer = (self.pointer + length) % self.size
        return data

    def stop(self) -> None:
        data = self.received[2:]
        self.received = bytearray()
        if not data:
            return
        base = self.pointer - self.pointer % self.page
        for offset, byte in enumerate(data):
            self.memory[base + (self.pointer - base + offset) % self.page] = byte
        self.pointer = base + (self.pointer - base + len(data)) % self.page
        self.cycle = _WRITE_CYCLE_POLLS


@dataclass
class _Target:
    """The `i2cs` scaffold: a 256-byte register file (the first byte of a write sets the pointer) or a sink that
    reads back the C.8 pattern; the counters of `i2cs.status`."""

    address: int
    sink: bool
    registers: bytearray = field(default_factory=lambda: bytearray(TARGET_REGISTERS))
    pointer: int = 0
    position: int = 0
    nack: int = 0
    addrnack: bool = False
    fault: bool = False
    faultat: int = 1
    pattern: str = "inc"
    seed: int = 0
    rx: int = 0
    tx: int = 0
    crc: int = 0
    writes: int = 0
    reads: int = 0
    stops: int = 0
    nacked: int = 0
    errors: int = 0
    last: bytearray = field(default_factory=bytearray)

    def acknowledge(self, read: bool) -> bool:
        if self.addrnack:
            return False
        self.position = 0
        if read:
            self.reads += 1
        else:
            self.writes += 1
            self.last = bytearray()
        return True

    def write(self, data: bytes) -> int:
        for index, byte in enumerate(data):
            self.position += 1
            if self.position == self.nack:
                self.nacked += 1
                return index
            self.rx += 1
            self.crc = zlib.crc32(bytes([byte]), self.crc)
            if len(self.last) < TARGET_LAST_MAX:
                self.last.append(byte)
            if self.sink:
                continue
            if self.position == 1:
                self.pointer = byte
            else:
                self.registers[self.pointer] = byte
                self.pointer = (self.pointer + 1) % TARGET_REGISTERS
        return len(data)

    def read(self, length: int) -> bytes | None:
        if self.fault and self.faultat <= length:
            self.fault = False
            return None
        self.tx += length
        if self.sink:
            return generate(length, self.pattern, self.seed)
        data = bytes(self.registers[(self.pointer + offset) % TARGET_REGISTERS] for offset in range(length))
        self.pointer = (self.pointer + length) % TARGET_REGISTERS
        return data

    def stop(self) -> None:
        self.stops += 1

    def status(self) -> str:
        return (
            f"OK rx={self.rx} tx={self.tx} crc={self.crc:08x} writes={self.writes} reads={self.reads} stops={self.stops}"
            f" nacked={self.nacked} errors={self.errors} last={self.last.hex()}"
        )

    def clear(self) -> None:
        self.rx = self.tx = self.crc = self.writes = self.reads = self.stops = self.nacked = self.errors = 0
        self.last = bytearray()


Device = _Chip | _Target


def _device(fw: Any, address: int) -> Device | None:
    """The device that answers `address` on the shared bus: the open target first, then the EEPROM chip."""
    target = fw.group("i2cs")
    if isinstance(target, FakeI2cTarget) and target.device is not None and target.device.address == address:
        return target.device
    eeprom = fw.group("eeprom")
    if isinstance(eeprom, FakeEeprom) and eeprom.chip.address == address:
        return eeprom.chip
    return None


def _write(fw: Any, address: int, data: bytes, stop: bool = True) -> tuple[int, str, bool]:
    """A master write with its address phase: (acknowledged data bytes, `complete`|`nack`, address acknowledged).
    A NACK ends the transfer with a STOP."""
    device = _device(fw, address)
    if device is None or not device.acknowledge(False):
        return 0, "nack", False
    sent = device.write(data)
    if sent < len(data):
        device.stop()
        return sent, "nack", True
    if stop:
        device.stop()
    return sent, "complete", True


def _read(fw: Any, address: int, length: int, stop: bool = True) -> tuple[bytes | None, str]:
    device = _device(fw, address)
    if device is None or not device.acknowledge(True):
        return None, "nack"
    data = device.read(length)
    if data is None:
        return None, "buserror"
    if stop:
        device.stop()
    return data, "complete"


def _payload(args: list[str], options: dict[str, str], position: int, capacity: int) -> bytes:
    """`ParsePayload` (C.8): hex, or `-` with `len`/`pattern`/`seed`; `-` alone is empty."""
    text = args[position]
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
    return generate(length, pattern, seed)


def _pins(fw: Any, options: dict[str, str], index: int) -> tuple[str, str]:
    scl = fw.pin(options["scl"])
    sda = fw.pin(options["sda"])
    fw.check_function("i2cScl", index, scl)
    fw.check_function("i2cSda", index, sda)
    return scl, sda


class FakeI2c(FakeGroup):
    prefix = "i2c"

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _OPEN_KEYS)
        index = _number(args[0], 0, _INSTANCES - 1)
        if "scl" not in options or "sda" not in options or ("freq" in options and "timing" in options):
            _fail("usage")
        pull = _choice(options, "pull", ("none", "up"), "none")
        raw = _number(options.get("timing", "0"), 0, UINT32_MAX)
        frequency = _number(options.get("freq", "0"), 0, UINT32_MAX)
        if index not in self.fw.spec.i2cs:
            _fail("range")
        kernel = self.fw.kernel_clock
        timing = DEFAULT_TIMING
        if "timing" in options:
            timing = raw & TIMING_MASK
        elif "freq" in options:
            computed = expected_timing(kernel, frequency)
            if computed is None:
                _fail("range")
            assert computed is not None
            timing = computed
        scl, sda = _pins(self.fw, options, index)
        state = {"index": index, "pull": pull, "continuing": None}
        self.fw.open_instance(self.prefix, str(index), state, [scl, sda], resources=[("i2c", index)])
        return f"OK timing=0x{timing:08x} kernel={kernel}"

    def _find(self, args: list[str], options: dict[str, str], keys: tuple[str, ...]) -> dict[str, Any]:
        _shape(args, options, 3, 3, keys)
        return self.fw.find_instance(self.prefix, str(_number(args[0], 0, _INSTANCES - 1)))

    def _hook(self, state: dict[str, Any], hook: str) -> None:
        self.fw.event(f"EVT i2c index={state['index']} hook={hook}")

    def _continued(self, state: dict[str, Any], read: bool) -> Device | None:
        """The device of a transfer that ended with `next=continue` in the same direction: no new address phase."""
        continuing, state["continuing"] = state["continuing"], None
        if continuing is None or continuing[1] != read:
            return None
        return _device(self.fw, continuing[0])

    def cmd_write(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, _WRITE_KEYS)
        address = _number(args[1], 0, _ADDRESS_MAX)
        action = _choice(options, "next", _NEXT, "stop")
        data = _payload(args, options, 2, I2C_BUFFER)
        device = self._continued(state, False)
        if device is not None:
            sent = device.write(data)
            result = "complete" if sent == len(data) else "nack"
            if result == "nack" or action == "stop":
                device.stop()
        else:
            sent, result, found = _write(self.fw, address, data, stop=action == "stop")
            if not found:
                self._hook(state, "notfound")
        if result == "complete" and action == "continue":
            state["continuing"] = (address, False)
        return f"OK sent={sent} result={result}"

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, _READ_KEYS)
        address = _number(args[1], 0, _ADDRESS_MAX)
        length = _number(args[2], 1, I2C_BUFFER)
        action = _choice(options, "next", _NEXT, "stop")
        output = _choice(options, "out", ("hex", "crc"), "hex")
        if output == "hex" and length > I2C_HEX_MAX:
            _fail("range")
        device = self._continued(state, True)
        if device is not None:
            data = device.read(length)
            result = "complete" if data is not None else "buserror"
            if data is not None and action == "stop":
                device.stop()
        else:
            data, result = _read(self.fw, address, length, stop=action == "stop")
        if result == "nack":
            self._hook(state, "notfound")
        elif result == "buserror":
            self._hook(state, "buserror")
        if data is not None and action == "continue":
            state["continuing"] = (address, True)
        if data is None:
            return f"OK result={result}"
        if output == "crc":
            return f"OK result={result} len={length} crc={crc_text(data)}"
        return f"OK result={result} data={data.hex()}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = str(_number(args[0], 0, _INSTANCES - 1))
        self.fw.find_instance(self.prefix, key)
        return self.fw.close_instance(self.prefix, key)


class FakeI2cTarget(FakeGroup):
    prefix = "i2cs"
    device: _Target | None = None

    def boot(self) -> None:
        self.device = None

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _TARGET_KEYS)
        index = _number(args[0], 0, _INSTANCES - 1)
        if "scl" not in options or "sda" not in options:
            _fail("usage")
        mode = _choice(options, "mode", ("regs", "sink"), "regs")
        _number(options.get("timing", "0"), 0, UINT32_MAX)
        address = _number(options.get("addr", str(_TARGET_ADDRESS)), _TARGET_ADDRESS_MIN, _TARGET_ADDRESS_MAX)
        if index not in self.fw.spec.i2cs or expected_timing(self.fw.kernel_clock, _TARGET_BUS) is None:
            _fail("range")
        scl, sda = _pins(self.fw, options, index)
        self.fw.open_instance(self.prefix, str(index), {"index": index}, [scl, sda], resources=[("i2c", index)])
        self.device = _Target(address, mode == "sink")
        return f"OK addr=0x{address:02x}"

    def _find(self, args: list[str], options: dict[str, str], low: int, high: int, keys: tuple[str, ...] = ()) -> _Target:
        _shape(args, options, low, high, keys)
        self.fw.find_instance(self.prefix, str(_number(args[0], 0, _INSTANCES - 1)))
        assert self.device is not None
        return self.device

    def cmd_cfg(self, args: list[str], options: dict[str, str]) -> str:
        device = self._find(args, options, 1, 1, _CONFIGURE_KEYS)
        nack = _number(options.get("nack", "0"), 0, _POSITION_MAX)
        addrnack = _number(options.get("addrnack", "0"), 0, 1) == 1
        _number(options.get("stretch", "0"), 0, _STRETCH_MAX)
        _number(options.get("stretchat", "0"), 0, _POSITION_MAX)
        fault = _choice(options, "fault", ("none", "stop"), "none") == "stop"
        faultat = _number(options.get("faultat", "1"), 1, _POSITION_MAX)
        pattern = _choice(options, "pattern", PATTERNS, "inc")
        seed = _number(options.get("seed", "0"), 0, SEED_MAX)
        device.nack, device.addrnack, device.fault, device.faultat = nack, addrnack, fault, faultat
        device.pattern, device.seed = pattern, seed
        return "OK"

    def cmd_status(self, args: list[str], options: dict[str, str]) -> str:
        device = self._find(args, options, 1, 1, ("clear",))
        clear = _number(options.get("clear", "0"), 0, 1) == 1
        line = device.status()
        if clear:
            device.clear()
        return line

    def cmd_regs(self, args: list[str], options: dict[str, str]) -> str:
        device = self._find(args, options, 3, 3)
        offset = _number(args[1], 0, TARGET_REGISTERS - 1)
        data = _hex(args[2], TARGET_DUMP_MAX)
        if not data:
            _fail("usage")
        if offset + len(data) > TARGET_REGISTERS:
            _fail("range")
        device.registers[offset : offset + len(data)] = data
        return "OK"

    def cmd_dump(self, args: list[str], options: dict[str, str]) -> str:
        device = self._find(args, options, 3, 3)
        offset = _number(args[1], 0, TARGET_REGISTERS - 1)
        length = _number(args[2], 1, TARGET_DUMP_MAX)
        if offset + length > TARGET_REGISTERS:
            _fail("range")
        return f"OK data={device.registers[offset : offset + length].hex()}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = str(_number(args[0], 0, _INSTANCES - 1))
        self.fw.find_instance(self.prefix, key)
        self.device = None
        return self.fw.close_instance(self.prefix, key)


@dataclass
class _Adapter:
    """`I2cEepromStm`: page writes with ACK polling, random reads with a repeated START."""

    address: int
    size: int
    page: int
    abytes: int
    wcycle: int
    failed: bool = False

    def word(self, address: int) -> bytes:
        return address.to_bytes(self.abytes, "big") if self.abytes == 2 else bytes([address & 0xFF])


class FakeEeprom(FakeGroup):
    """`eeprom.attach`/`eeprom.detach` and EMIL's `eeprom.write`/`eeprom.read`/`eeprom.erase` over the adapter. An
    error prints `EVT eeprom error=... address=...` and answers `ERR timeout` at once (the firmware after 5 s); the
    group stays busy until `eeprom.detach`."""

    prefix = "eeprom"
    adapter: _Adapter | None = None

    def __init__(self, fw: Any) -> None:
        super().__init__(fw)
        self.chip = _Chip()

    def boot(self) -> None:
        self.adapter = None
        self.chip.cycle = 0
        self.chip.received = bytearray()

    def poll(self) -> None:
        self.chip.cycle = 0

    def cmd_attach(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _ATTACH_KEYS)
        index = _number(args[0], 0, _INSTANCES - 1)
        if "scl" not in options or "sda" not in options:
            _fail("usage")
        address, size, page, abytes, wcycle, frequency = (
            _number(options.get(key, default), 0, UINT32_MAX)
            for key, default in (("addr", "0x50"), ("size", "32768"), ("page", "64"), ("abytes", "2"), ("wcycle", "10"), ("freq", "400000"))
        )
        if index not in self.fw.spec.i2cs or not _TARGET_ADDRESS_MIN <= address <= _TARGET_ADDRESS_MAX or not 1 <= size <= _EEPROM_SIZE_MAX:
            _fail("range")
        if not _EEPROM_PAGE_MIN <= page <= _EEPROM_PAGE_MAX or page & (page - 1) or abytes not in (1, 2):
            _fail("range")
        if wcycle > _EEPROM_WRITE_CYCLE_MAX:
            _fail("range")
        if (abytes == 1 and size > _ONE_BYTE_SPACE) or not BUS_MIN <= frequency <= BUS_MAX:
            _fail("range")
        if expected_timing(self.fw.kernel_clock, frequency) is None:
            _fail("range")
        scl, sda = _pins(self.fw, options, index)
        self.fw.open_instance(self.prefix, "attached", {"index": index}, [scl, sda], resources=[("i2c", index)])
        self.adapter = _Adapter(address, size, page, abytes, wcycle)
        return "OK"

    def cmd_detach(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self.fw.find_instance(self.prefix, "attached")
        self.adapter = None
        return self.fw.close_instance(self.prefix, "attached")

    def _size(self) -> int:
        return 0 if self.adapter is None else self.adapter.size

    def _check_busy(self) -> None:
        if self.adapter is not None and self.adapter.failed:
            _fail("busy")

    def _fail(self, adapter: _Adapter, result: str, address: int) -> str:
        adapter.failed = True
        self.fw.event(f"EVT eeprom error={result} address={address}")
        return "ERR timeout"

    def cmd_write(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        address = _number(args[0], 0, self._size())
        data = _hex(args[1], EEPROM_BUFFER)
        if not data:
            _fail("usage")
        if len(data) > self._size() - address:
            _fail("range")
        self._check_busy()
        assert self.adapter is not None
        return self._write(self.adapter, address, data)

    def _write(self, adapter: _Adapter, address: int, data: bytes) -> str:
        while data:
            chunk = min(len(data), adapter.page - address % adapter.page)
            _, result, _ = _write(self.fw, adapter.address, adapter.word(address) + data[:chunk])
            if result != "complete":
                return self._fail(adapter, result, address)
            if adapter.wcycle:
                for _ in range(_POLLS_MAX):
                    if _write(self.fw, adapter.address, adapter.word(address))[1] == "complete":
                        break
                else:
                    return self._fail(adapter, "nack", address)
            address += chunk
            data = data[chunk:]
        return "OK"

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        address = _number(args[0], 0, self._size())
        length = _number(args[1], 1, EEPROM_BUFFER)
        if length > self._size() - address:
            _fail("range")
        self._check_busy()
        adapter = self.adapter
        assert adapter is not None
        _, result, _ = _write(self.fw, adapter.address, adapter.word(address), stop=False)
        if result != "complete":
            return self._fail(adapter, result, address)
        data, result = _read(self.fw, adapter.address, length)
        if data is None:
            return self._fail(adapter, result, address)
        return f"OK data={data.hex()}"

    def cmd_erase(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._check_busy()
        if self.adapter is None:
            return "OK"
        return self._write(self.adapter, 0, b"\xff" * self.adapter.size)
