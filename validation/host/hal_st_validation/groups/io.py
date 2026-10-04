"""I/O groups of validation/PROTOCOL.md: `sgpio` (`hal::SynchronousOutputPinStm`, `hal::SmallPeripheralPinStm`,
`hal::MultiGpioPinStm` with `hal::MultiPeripheralPinStm`) and `clock` (the hal-st default clock configuration, MCO and
HSI48 scaffolding on STM32WB55), with the expectations of the clock and unique device ID tests (pure functions).

The send-only UART (`hal::SynchronousUartStmSendOnly`) is `fw.uart.open(..., sendonly=True)`.

Sources: `SyncGpioGroup.cpp`, `ClockGroup.cpp`; the unique device ID layout of RM0434 / RM0493 ("Unique device ID
register (96 bits)"): bytes 0-3 the X and Y wafer coordinates, byte 4 the wafer number, bytes 5-11 the lot number in
ASCII.
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal

from ad3_waveforms_bench import analysis

from .base import Group, Pin

if TYPE_CHECKING:
    from ..firmware import Firmware

__all__ = [
    "GROUPS",
    "MCO_DIVIDERS",
    "MCO_SOURCES",
    "SPEEDS",
    "UID_BYTES",
    "UID_LOT",
    "Clock",
    "ClockInfo",
    "SyncGpio",
    "Uid",
    "edge_fit_frequency",
    "parse_uid",
]

Speed = Literal["low", "medium", "fast", "high"]
McoSource = Literal["sysclk", "hse", "hsi", "lse", "hsi48", "off"]

SPEEDS: tuple[Speed, ...] = ("low", "medium", "fast", "high")
MCO_SOURCES: tuple[McoSource, ...] = ("sysclk", "hse", "hsi", "lse", "hsi48", "off")
MCO_DIVIDERS = (1, 2, 4, 8, 16)
UID_BYTES = 12
UID_LOT = slice(5, 12)
_CLOCK_FLAGS = ("hse", "lse", "hsi", "hsi48", "pll")


class SyncGpio(Group):
    """`sgpio`: one owner for every pin it holds; `release` of any pin of a `multi` set releases the whole set."""

    prefix = "sgpio"

    def __init__(self, firmware: Firmware) -> None:
        super().__init__(firmware)
        self._multi: tuple[str, ...] = ()

    def out(self, pin: Pin, value: int | bool, od: bool | None = None, speed: Speed | None = None) -> None:
        """The first use constructs the pin; `od` and `speed` rebuild it when they change, else they stay."""
        pin = self._fw.pin(pin)
        self._cmd("out", pin, int(bool(value)), od=od, speed=speed)
        self._fw.track(("sgpio", pin), "sgpio.release", pin)

    def latch(self, pin: Pin) -> int:
        return self._cmd("latch", self._fw.pin(pin)).as_int("value")

    def af(self, pin: Pin, timer: int | None = None, ch: int | None = None, af: int | None = None) -> int:
        """Mux `pin` to channel `ch` of TIM`timer` (the alternate function of the pinout table) or to the raw `af`;
        returns the alternate function number."""
        pin = self._fw.pin(pin)
        response = self._cmd("af", pin, timer=timer, ch=ch, af=af)
        self._fw.track(("sgpio", pin), "sgpio.release", pin)
        return response.as_int("af")

    def multi(self, pins: Sequence[Pin], timer: int, ch: int | None = None) -> None:
        resolved = tuple(self._fw.pin(pin) for pin in pins)
        self._cmd("multi", list(resolved), timer=timer, ch=ch)
        self._multi = resolved
        self._fw.track(("sgpio", "multi"), "sgpio.release", resolved[0])

    def release(self, pin: Pin) -> None:
        pin = self._fw.pin(pin)
        self._cmd("release", pin)
        self._fw.untrack(("sgpio", pin))
        if pin in self._multi:
            self._multi = ()
            self._fw.untrack(("sgpio", "multi"))


@dataclass(frozen=True)
class ClockInfo:
    """`clock.info`: bus clocks in Hz, oscillator ready flags, the RNG kernel clock selection and (STM32WB) the
    CLK48 source; `pclk7` and the `hsi48` flag only where the MCU has them."""

    sysclk: int
    hclk: int
    pclk1: int
    pclk2: int
    pclk7: int | None
    flags: dict[str, int]
    rngsel: str
    clk48: str | None
    raw: str


class Clock(Group):
    prefix = "clock"

    def info(self) -> ClockInfo:
        response = self._cmd("info")
        pclk7 = response.get("pclk7")
        return ClockInfo(
            sysclk=response.as_int("sysclk"),
            hclk=response.as_int("hclk"),
            pclk1=response.as_int("pclk1"),
            pclk2=response.as_int("pclk2"),
            pclk7=None if pclk7 is None else int(pclk7),
            flags={flag: response.as_int(flag) for flag in _CLOCK_FLAGS if flag in response},
            rngsel=response["rngsel"],
            clk48=response.get("clk48"),
            raw=response.raw,
        )

    def mco(self, source: McoSource, div: int | None = None) -> None:
        """STM32WB55 only; the pin is muxed separately (`sgpio.af PA8 af=0`). Anything but `off` is undone by
        `close_all`."""
        self._cmd("mco", source, div=div)
        if source == "off":
            self._fw.untrack(("clock", "mco"))
        else:
            self._fw.track(("clock", "mco"), "clock.mco", "off")

    def hsi48(self, on: bool) -> None:
        """STM32WB55 only: switch HSI48 and wait for its ready flag; `close_all` switches it back on."""
        self._cmd("hsi48", int(bool(on)))
        if on:
            self._fw.untrack(("clock", "hsi48"))
        else:
            self._fw.track(("clock", "hsi48"), "clock.hsi48", 1)


def edge_fit_frequency(bits: Sequence[int], rate: float) -> float:
    """Frequency of a periodic signal from a least-squares line through its rising edge times (edge k at t0 + k T):
    far more precise than first-to-last edge counting for long captures, and robust to the sample quantisation."""
    times = [edge.index / rate for edge in analysis.edges(bits) if edge.rising]
    if len(times) < 3:
        raise ValueError("fewer than three rising edges")
    rough = (times[-1] - times[0]) / (len(times) - 1)
    periods = [round((time - times[0]) / rough) for time in times]
    count = len(times)
    mean_k = sum(periods) / count
    mean_t = sum(times) / count
    covariance = sum((k - mean_k) * (t - mean_t) for k, t in zip(periods, times))
    variance = sum((k - mean_k) ** 2 for k in periods)
    return variance / covariance


@dataclass(frozen=True)
class Uid:
    raw: bytes
    x: int
    y: int
    wafer: int
    lot: bytes


def parse_uid(text: str) -> Uid:
    """`info uid=<24 hex digits>`: the 96-bit unique device ID as the firmware prints its bytes, lowest address
    first (X in bytes 0-1, Y in bytes 2-3, little endian)."""
    raw = bytes.fromhex(text)
    if len(raw) != UID_BYTES:
        raise ValueError(f"uid has {len(raw)} bytes, not {UID_BYTES}")
    return Uid(
        raw=raw,
        x=int.from_bytes(raw[0:2], "little"),
        y=int.from_bytes(raw[2:4], "little"),
        wafer=raw[4],
        lot=raw[UID_LOT],
    )


GROUPS: dict[str, type[Group]] = {"sgpio": SyncGpio, "clock": Clock}
