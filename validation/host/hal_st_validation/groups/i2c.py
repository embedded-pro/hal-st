"""I2C groups of validation/PROTOCOL.md: `i2c` (master, `hal::I2cStm`), `i2cs` (the LL target scaffold on another
instance) and `eeprom` (`eeprom.attach`/`eeprom.detach` of validation/firmware/EepromGroup.cpp around EMIL's
`eeprom.write`/`eeprom.read`/`eeprom.erase` over the `I2cEepromStm` adapter).

Sources: validation/firmware/I2cGroup.cpp (`i2c.*` answers within 1000 ms or `ERR timeout`; `EVT i2c index=<i>
hook=notfound|buserror|arblost` precedes the final line of the transfer), I2cTarget.cpp, EepromGroup.cpp and
I2cEepromStm.cpp; EMIL `HilEepromCommands.cpp` (128-byte buffer, 5 s timeout).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from ad3_waveforms_bench.protocol import Event

from ..patterns import Pattern
from .base import Group, Pin

__all__ = [
    "DEFAULT_TIMING",
    "EEPROM_BUFFER",
    "EEPROM_TIMEOUT_S",
    "GROUPS",
    "I2C_BUFFER",
    "I2C_HEX_MAX",
    "I2C_TIMEOUT_S",
    "TARGET_DUMP_MAX",
    "TARGET_LAST_MAX",
    "TARGET_REGISTERS",
    "TIMING_MASK",
    "Eeprom",
    "I2c",
    "I2cOpen",
    "I2cRead",
    "I2cTarget",
    "I2cWrite",
    "Next",
    "TargetStatus",
]

Next = Literal["stop", "restart", "continue"]

# `hal::I2cStm::Config::timing` on STM32WB/WBA (B.1f): Standard mode for every kernel clock up to 100 MHz.
DEFAULT_TIMING = 0x70B03D3D
# `HAL_I2C_Init` drops TIMINGR bits 24-27 (reserved); `timing=` replies the register as written.
TIMING_MASK = 0xF0FFFFFF
I2C_BUFFER = 1024
I2C_HEX_MAX = 128
I2C_TIMEOUT_S = 1.0
TARGET_REGISTERS = 256
TARGET_DUMP_MAX = 128
TARGET_LAST_MAX = 32
EEPROM_BUFFER = 128
EEPROM_TIMEOUT_S = 5.0


@dataclass(frozen=True)
class I2cOpen:
    timing: int
    kernel: int


@dataclass(frozen=True)
class I2cWrite:
    """`sent` is `numberOfBytesSent` of `hal::I2cMaster::SendData`: the acknowledged data bytes; `result` is
    `complete`, `nack` or `buserror`."""

    sent: int
    result: str


@dataclass(frozen=True)
class I2cRead:
    """`data` (hex output) or `length` and `crc` (`out=crc`) when `result` is complete, else only `result`."""

    result: str
    data: bytes | None = None
    length: int | None = None
    crc: str | None = None


@dataclass(frozen=True)
class TargetStatus:
    rx: int
    tx: int
    crc: str
    writes: int
    reads: int
    stops: int
    nacked: int
    errors: int
    last: bytes


class I2c(Group):
    prefix = "i2c"

    def _timeout(self, cmd_timeout: float | None) -> float:
        return cmd_timeout or self._fw.terminal.timeout + I2C_TIMEOUT_S

    def open(
        self,
        index: int,
        scl: Pin,
        sda: Pin,
        freq: int | None = None,
        timing: int | None = None,
        pull: Literal["none", "up"] | None = None,
    ) -> I2cOpen:
        response = self._cmd(
            "open",
            index,
            scl=self._pin(scl),
            sda=self._pin(sda),
            freq=freq,
            timing=None if timing is None else f"0x{timing:08x}",
            pull=pull,
        )
        self._fw.track((self.prefix, index), f"{self.prefix}.close", index)
        return I2cOpen(response.as_int("timing"), response.as_int("kernel"))

    def write(
        self,
        index: int,
        address: int,
        data: bytes = b"",
        next: Next | None = None,
        len: int | None = None,
        pattern: Pattern | None = None,
        seed: int | None = None,
        cmd_timeout: float | None = None,
    ) -> I2cWrite:
        """`data` empty and no `len` is a zero-length write (an address probe)."""
        response = self._cmd(
            "write",
            index,
            f"0x{address:02x}",
            data,
            next=next,
            len=len,
            pattern=pattern,
            seed=seed,
            cmd_timeout=self._timeout(cmd_timeout),
        )
        return I2cWrite(response.as_int("sent"), response["result"])

    def read(
        self,
        index: int,
        address: int,
        length: int,
        next: Next | None = None,
        out: Literal["hex", "crc"] | None = None,
        cmd_timeout: float | None = None,
    ) -> I2cRead:
        response = self._cmd("read", index, f"0x{address:02x}", length, next=next, out=out, cmd_timeout=self._timeout(cmd_timeout))
        result = response["result"]
        if "crc" in response:
            return I2cRead(result, length=response.as_int("len"), crc=response["crc"])
        if "data" in response:
            return I2cRead(result, data=response.as_bytes("data"))
        return I2cRead(result)

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack((self.prefix, index))

    def events(self, index: int) -> list[Event]:
        """The `EVT i2c` lines of `index` received so far, removed from the terminal's queue."""
        return [event for event in self._fw.terminal.drain_events("i2c") if event.as_int("index") == index]

    def hooks(self, index: int) -> list[str]:
        """The driver hooks (`notfound`, `buserror`, `arblost`) reported for `index` since the last call."""
        return [event["hook"] for event in self.events(index)]


class I2cTarget(Group):
    prefix = "i2cs"

    def open(
        self,
        index: int,
        scl: Pin,
        sda: Pin,
        addr: int | None = None,
        mode: Literal["regs", "sink"] | None = None,
        timing: int | None = None,
    ) -> int:
        """Returns the own address."""
        response = self._cmd(
            "open",
            index,
            scl=self._pin(scl),
            sda=self._pin(sda),
            addr=None if addr is None else f"0x{addr:02x}",
            mode=mode,
            timing=None if timing is None else f"0x{timing:08x}",
        )
        self._fw.track((self.prefix, index), f"{self.prefix}.close", index)
        return response.as_int("addr")

    def cfg(
        self,
        index: int,
        nack: int | None = None,
        addrnack: bool | None = None,
        stretch: int | None = None,
        stretchat: int | None = None,
        fault: Literal["none", "stop"] | None = None,
        faultat: int | None = None,
        pattern: Pattern | None = None,
        seed: int | None = None,
    ) -> None:
        """Replaces the whole behaviour: options left out take their defaults (`nack=0 addrnack=0 stretch=0
        stretchat=0 fault=none faultat=1 pattern=inc seed=0`)."""
        self._cmd(
            "cfg",
            index,
            nack=nack,
            addrnack=addrnack,
            stretch=stretch,
            stretchat=stretchat,
            fault=fault,
            faultat=faultat,
            pattern=pattern,
            seed=seed,
        )

    def status(self, index: int, clear: bool | None = None) -> TargetStatus:
        response = self._cmd("status", index, clear=clear)
        return TargetStatus(
            rx=response.as_int("rx"),
            tx=response.as_int("tx"),
            crc=response["crc"],
            writes=response.as_int("writes"),
            reads=response.as_int("reads"),
            stops=response.as_int("stops"),
            nacked=response.as_int("nacked"),
            errors=response.as_int("errors"),
            last=response.as_bytes("last"),
        )

    def regs(self, index: int, offset: int, data: bytes) -> None:
        self._cmd("regs", index, offset, data)

    def dump(self, index: int, offset: int, length: int) -> bytes:
        return self._cmd("dump", index, offset, length).as_bytes("data")

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack((self.prefix, index))


class Eeprom(Group):
    prefix = "eeprom"

    def _timeout(self, cmd_timeout: float | None) -> float:
        return cmd_timeout or self._fw.terminal.timeout + EEPROM_TIMEOUT_S

    def attach(
        self,
        index: int,
        scl: Pin,
        sda: Pin,
        addr: int | None = None,
        size: int | None = None,
        page: int | None = None,
        abytes: Literal[1, 2] | None = None,
        freq: int | None = None,
        wcycle: int | None = None,
    ) -> None:
        self._cmd(
            "attach",
            index,
            scl=self._pin(scl),
            sda=self._pin(sda),
            addr=None if addr is None else f"0x{addr:02x}",
            size=size,
            page=page,
            abytes=abytes,
            freq=freq,
            wcycle=wcycle,
        )
        self._fw.track((self.prefix,), f"{self.prefix}.detach")

    def detach(self, cmd_timeout: float | None = None) -> None:
        self._cmd("detach", cmd_timeout=cmd_timeout)
        self._fw.untrack((self.prefix,))

    def write(self, address: int, data: bytes, cmd_timeout: float | None = None) -> None:
        """EMIL `eeprom.write`: at most `EEPROM_BUFFER` bytes, written page by page with ACK polling."""
        self._cmd("write", address, data, cmd_timeout=self._timeout(cmd_timeout))

    def read(self, address: int, length: int, cmd_timeout: float | None = None) -> bytes:
        return self._cmd("read", address, length, cmd_timeout=self._timeout(cmd_timeout)).as_bytes("data")

    def erase(self, cmd_timeout: float | None = None) -> None:
        """Every byte of the attached `size` to 0xFF, a page at a time."""
        self._cmd("erase", cmd_timeout=self._timeout(cmd_timeout))

    def errors(self) -> list[Event]:
        """The `EVT eeprom error=<nack|buserror> address=<a>` lines received so far."""
        return self._fw.terminal.drain_events("eeprom")


GROUPS: dict[str, type[Group]] = {"i2c": I2c, "i2cs": I2cTarget, "eeprom": Eeprom}
