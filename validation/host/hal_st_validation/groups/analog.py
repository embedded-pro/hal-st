"""Analog input and DMA groups of validation/PROTOCOL.md: `ain` (`hal::AnalogToDigitalPinImplStm`,
`hal::AnalogToDigitalInternalTemperatureStm`, `hal::AdcTriggeredByTimerWithDma`) and `dma`
(`hal::CircularTransmitDmaChannel`), with the expected waveforms (pure functions).

Sources: `validation/firmware/AnalogInputGroup.cpp` (one driver per command; `ain.burst` builds one
`AdcTriggeredByTimerWithDma` over a 256-sample buffer and calls `Measure(n)` `repeat` times, paced by TIM2 through
`TriggerTiming`), `validation/firmware/DmaGroup.cpp` (TIM2 update requests write one BSRR word per pattern bit,
least significant bit of each byte first).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from ad3_waveforms_bench.protocol import Event, Response

from .base import Group, Pin

__all__ = [
    "AIN_BUFFER",
    "AIN_LIST_MAX",
    "AIN_RATE_MAX",
    "AIN_RATE_MIN",
    "AIN_REPEAT_MAX",
    "DMA_PATTERN_MAX",
    "DMA_RATE_MAX",
    "DMA_WAVE_DEFAULT_MS",
    "DMA_WAVE_MAX_MS",
    "GROUPS",
    "AinReading",
    "AnalogInput",
    "BurstRun",
    "Dma",
    "burst_seconds",
    "trigger_rate",
    "trigger_timing",
    "wave_bits",
]

Output = Literal["list", "stats"]

AIN_BUFFER = 256
AIN_LIST_MAX = 64
AIN_RATE_MIN = 10
AIN_RATE_MAX = 100_000
AIN_REPEAT_MAX = 2
DMA_RATE_MAX = 1_000_000
DMA_PATTERN_MAX = 32
DMA_WAVE_DEFAULT_MS = 50
DMA_WAVE_MAX_MS = 10_000
# Extra time `ain.burst` allows itself on top of `repeat` measurements (AnalogInputGroup.cpp `burstMarginMs`).
_BURST_MARGIN_S = 1.0


def trigger_timing(clock: int, rate: int, counter_bits: int = 32) -> tuple[int, int]:
    """`TriggerTiming` (AdcFactory.cpp): (PSC, ARR) of the smallest prescaler that fits, with the tick count rounded
    to the nearer of floor and ceiling, at least 2 ticks."""
    maximum_ticks = 1 << counter_bits
    prescale = clock // (rate * (maximum_ticks + 1)) + 1
    floor_ticks = max(clock // (prescale * rate), 2)
    ceil_ticks = min(floor_ticks + 1, maximum_ticks)
    round_up = abs(clock - prescale * ceil_ticks * rate) * floor_ticks < abs(clock - prescale * floor_ticks * rate) * ceil_ticks
    return prescale - 1, (ceil_ticks if round_up else floor_ticks) - 1


def trigger_rate(clock: int, rate: int, counter_bits: int = 32) -> float:
    """Update events per second the timer really runs at for a requested `rate` (TIM2 counts 32 bits)."""
    prescaler, period = trigger_timing(clock, rate, counter_bits)
    return clock / ((prescaler + 1) * (period + 1))


def burst_seconds(samples: int, clock: int, rate: int) -> float:
    """Duration of one `Measure(samples)`: one conversion per TIM2 update, the first one period after the start."""
    return samples / trigger_rate(clock, rate)


def wave_bits(pattern: bytes) -> list[int]:
    """The levels `dma.wave` drives, one per update: byte by byte, least significant bit first."""
    return [(byte >> bit) & 1 for byte in pattern for bit in range(8)]


@dataclass(frozen=True)
class AinReading:
    code: int
    # Whole degrees Celsius times 1000 (`__LL_ADC_CALC_TEMPERATURE` returns whole degrees); only for `temp`.
    mcelsius: int | None


@dataclass(frozen=True)
class BurstRun:
    """One `Measure(n)` of `ain.burst`: `samples` with `out=list` (None with `out=stats`), the statistics the
    firmware reports (or computes the same way: mean rounded half up), and `us`, the measurement time."""

    n: int
    minimum: int
    maximum: int
    mean: int
    us: int
    samples: tuple[int, ...] | None

    @classmethod
    def from_values(cls, source: Response | Event) -> BurstRun:
        us = source.as_int("us")
        if "samples" in source:
            samples = tuple(source.as_ints("samples"))
            mean = (sum(samples) + len(samples) // 2) // len(samples)
            return cls(n=len(samples), minimum=min(samples), maximum=max(samples), mean=mean, us=us, samples=samples)
        return cls(
            n=source.as_int("n"),
            minimum=source.as_int("min"),
            maximum=source.as_int("max"),
            mean=source.as_int("mean"),
            us=us,
            samples=None,
        )


class AnalogInput(Group):
    prefix = "ain"

    def read(self, adc: int, target: Pin, sampling: str | float | None = None) -> AinReading:
        """`target` is a pin or `"temp"` (the internal temperature sensor); `sampling` in ADC clock cycles."""
        source = "temp" if target == "temp" else self._pin(target)
        response = self._cmd("read", adc, source, sampling=sampling)
        mcelsius = response.get("mcelsius")
        return AinReading(code=response.as_int("code"), mcelsius=None if mcelsius is None else int(mcelsius))

    def burst(
        self,
        adc: int,
        pin: Pin,
        n: int,
        rate: int,
        repeat: int | None = None,
        out: Output | None = None,
        cmd_timeout: float | None = None,
    ) -> list[BurstRun]:
        """One run per `Measure(n)`: every run but the last arrives as `EVT ain index=<adc> run=<k>`, the last one is
        the final line."""
        timeout = cmd_timeout or self._fw.terminal.timeout + (repeat or 1) * n / rate + _BURST_MARGIN_S
        response = self._cmd("burst", adc, self._pin(pin), n=n, rate=rate, repeat=repeat, out=out, cmd_timeout=timeout)
        events = [event for event in self._fw.terminal.drain_events("ain") if event.as_int("index") == adc]
        return [BurstRun.from_values(event) for event in events] + [BurstRun.from_values(response)]


class Dma(Group):
    prefix = "dma"

    def wave(self, pin: Pin, rate: int, pattern: bytes, ms: int | None = None, cmd_timeout: float | None = None) -> None:
        """Drives `pattern` on `pin` at `rate` bits per second for `ms` (default 50) and answers afterwards."""
        duration = (ms or DMA_WAVE_DEFAULT_MS) / 1000
        timeout = cmd_timeout or self._fw.terminal.timeout + duration
        self._cmd("wave", self._pin(pin), rate=rate, pattern=pattern, ms=ms, cmd_timeout=timeout)


GROUPS: dict[str, type[Group]] = {"ain": AnalogInput, "dma": Dma}
