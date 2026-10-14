"""`hrtim` group of validation/PROTOCOL.md: one timing unit A-F of the STM32G474 HRTIM (`hal::HighResolutionPwmStm`)
at a time, its two outputs in phase, complementary with dead time or shifted by a phase, a fault input that forces
them inactive, and an ADC trigger on its period that `hrtim.adc` counts with ADC1. The functions below are the
driver's arithmetic, which the fake and the bench tests share."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from .base import Group, Pin

__all__ = [
    "ADC1_TRIGGERS",
    "GROUPS",
    "Hrtim",
    "HrtimTiming",
    "adc_trigger_available",
    "compare_ticks",
    "dead_time",
    "dead_time_clock",
    "duty_cycle",
    "format_percent",
    "frequency_range",
    "high_resolution_clock",
    "minimum_ticks",
    "timing_for",
]

Mode = Literal["inphase", "comp", "shift"]
Outputs = Literal["both", "1", "2"]

MAXIMUM_PERIOD = 0xFFDF
PRESCALER_RATIOS = 8
MAXIMUM_DEAD_TIME_TICKS = 511
UNITS = 6
FAULT_INPUTS = 6
ADC_TRIGGERS = 10
# validation/firmware/boards/stm32g474/BoardProfile.hpp hrtimAdcTriggers: the HRTIM triggers ADC1 converts on.
ADC1_TRIGGERS = (1, 3, 5, 6, 7, 8, 9, 10)


def high_resolution_clock(clock: int, ratio: int) -> int:
    """fHRCK: fHRTIM x 32 (the DLL) divided by 2^ratio, ratio 0 (MUL32) to 7 (DIV4)."""
    return (clock << 5) >> ratio


def minimum_ticks(ratio: int) -> int:
    """The smallest period or compare distance the timer takes: three fHRTIM periods, at least 3 ticks."""
    return max(3, (3 << 5) >> ratio)


def timing_for(clock: int, frequency: int) -> tuple[int, int] | None:
    """`HighResolutionPwmStm::TimingFor`: (ratio, period) of the finest prescaler whose rounded period fits and whose
    exact period holds two `minimum_ticks`."""
    if frequency == 0:
        return None
    for ratio in range(PRESCALER_RATIOS):
        hrck = high_resolution_clock(clock, ratio)
        period = (hrck + frequency // 2) // frequency
        if period <= MAXIMUM_PERIOD and hrck >= 2 * minimum_ticks(ratio) * frequency:
            return ratio, period
    return None


def frequency_range(clock: int) -> tuple[int, int]:
    """The lowest and highest frequency `timing_for` accepts: the longest period at DIV4, the shortest at MUL32."""
    lowest = max(1, high_resolution_clock(clock, PRESCALER_RATIOS - 1) // MAXIMUM_PERIOD)
    while timing_for(clock, lowest) is None:
        lowest += 1
    highest = high_resolution_clock(clock, 0) // (2 * minimum_ticks(0))
    while timing_for(clock, highest + 1) is not None:
        highest += 1
    while timing_for(clock, highest) is None:
        highest -= 1
    return lowest, highest


DUTY_FULL_SCALE = 1 << 16


def duty_cycle(percent: str | float) -> int | None:
    """`HilArguments::ParseDutyCycle`: the `hal::DutyCycle` (16 fractional bits) of a decimal percent 0-100 with up to
    4 decimals, else None (`ERR usage`)."""
    text = percent if isinstance(percent, str) else format_percent(percent)
    integer, dot, fraction = text.partition(".")
    if not integer.isdigit() or (dot and not (1 <= len(fraction) <= 4 and fraction.isdigit())):
        return None
    scaled = int(integer) * 10_000 + int(fraction.ljust(4, "0") or 0)
    if scaled > 100 * 10_000:
        return None
    return (scaled * DUTY_FULL_SCALE + 500_000) // 1_000_000


def format_percent(percent: float) -> str:
    """A percent as `ParseDutyCycle` takes it: at most 4 decimals."""
    return f"{percent:.4f}".rstrip("0").rstrip(".")


def compare_ticks(period: int, ratio: int, duty: int) -> int:
    """`DutyCycle::ToCounts` of a duty or phase over the period, kept `minimum_ticks` away from both ends."""
    minimum = minimum_ticks(ratio)
    ticks = (period * duty + DUTY_FULL_SCALE // 2) >> 16
    return min(max(ticks, minimum), period - minimum)


def dead_time_clock(clock: int, prescaler: int) -> int:
    """fDTG: fHRTIM x 8 divided by 2^prescaler, prescaler 0 (MUL8) to 7 (DIV16)."""
    return (clock << 3) >> prescaler


def dead_time(clock: int, rising_ns: int, falling_ns: int) -> tuple[int, int, int]:
    """(prescaler, rising ticks, falling ticks): the finest prescaler whose ticks fit the longer dead time."""
    longest = max(rising_ns, falling_ns)
    prescaler = 0
    while prescaler + 1 < PRESCALER_RATIOS and longest * dead_time_clock(clock, prescaler) // 1_000_000_000 > MAXIMUM_DEAD_TIME_TICKS:
        prescaler += 1
    step = dead_time_clock(clock, prescaler)

    def ticks(ns: int) -> int:
        return min((ns * step + 500_000_000) // 1_000_000_000, MAXIMUM_DEAD_TIME_TICKS)

    return prescaler, ticks(rising_ns), ticks(falling_ns)


def adc_trigger_available(unit: int, trigger: int) -> bool:
    """Triggers 2, 4, 6, 8 and 10 have no period event of unit E (5)."""
    return 1 <= trigger <= ADC_TRIGGERS and not (unit == 5 and trigger % 2 == 0)


@dataclass(frozen=True)
class HrtimTiming:
    """The reply of `hrtim.open` and `hrtim.freq`: fHRTIM, the prescaler ratio, the period in fHRCK ticks, the
    rising dead time in fDTG ticks and the dead-time prescaler."""

    clock: int
    ratio: int
    period: int
    dead: int
    dtprsc: int

    @property
    def frequency(self) -> float:
        return high_resolution_clock(self.clock, self.ratio) / self.period

    @property
    def tick_s(self) -> float:
        return 1 / high_resolution_clock(self.clock, self.ratio)

    @property
    def dead_tick_s(self) -> float:
        return 1 / dead_time_clock(self.clock, self.dtprsc)


class Hrtim(Group):
    prefix = "hrtim"

    def open(
        self,
        unit: int,
        *,
        out: Outputs | None = None,
        mode: Mode | None = None,
        freq: int | None = None,
        dead: int | None = None,
        deadfall: int | None = None,
        flt: Pin | None = None,
        fltpol: Literal["low", "high"] | None = None,
        adc: int | None = None,
    ) -> HrtimTiming:
        response = self._cmd(
            "open", unit, out=out, mode=mode, freq=freq, dead=dead, deadfall=deadfall, flt=self._pin(flt), fltpol=fltpol, adc=adc
        )
        self._fw.track(("hrtim", unit), "hrtim.close", unit)
        return self._timing(response)

    def duty(self, unit: int, *percents: float | str) -> list[int]:
        """Starts the unit (`Start` of `hal::SingleChannelPwm` or `TwoChannelsPwm`); the compares of its channels."""
        texts = [percent if isinstance(percent, str) else format_percent(percent) for percent in percents]
        return self._cmd("duty", unit, *texts).as_ints("compare")

    def phase(self, unit: int, percent: float | str) -> None:
        self._cmd("phase", unit, percent if isinstance(percent, str) else format_percent(percent))

    def freq(self, unit: int, hz: int) -> HrtimTiming:
        return self._timing(self._cmd("freq", unit, hz))

    def stop(self, unit: int) -> None:
        self._cmd("stop", unit)

    def fault(self, unit: int) -> bool:
        return self._cmd("fault", unit).as_int("active") == 1

    def adc(self, unit: int, samples: int) -> int:
        """Microseconds from the first to the last of `samples` conversions."""
        return self._cmd("adc", unit, samples).as_int("us")

    def close(self, unit: int) -> None:
        self._cmd("close", unit)
        self._fw.untrack(("hrtim", unit))

    @staticmethod
    def _timing(response) -> HrtimTiming:
        return HrtimTiming(*(response.as_int(key) for key in ("clock", "ratio", "period", "dead", "dtprsc")))


GROUPS: dict[str, type[Group]] = {"hrtim": Hrtim}
