"""Fake `ain` and `dma` groups: the argument checks of validation/firmware/AnalogInputGroup.cpp and DmaGroup.cpp in
their order, and what they share with `adc`, `pwm` and the other timer users: the ADC and its DMA channel
(`ResourceAllocation` adc, dma1 7), TIM2 (`TimerAllocation`) and the `dma.wave` channel (WB55 DMA2 4, WBA55 GPDMA1 8).

`ain.burst` and `dma.wave` answer later, as the firmware does: they hold their claims until the fake clock reaches
the end of the measurements or of the wave, and `poll()` prints the `EVT ain` lines and the final line then."""

from __future__ import annotations

from dataclasses import dataclass
from typing import ClassVar

from ..groups.analog import (
    AIN_BUFFER,
    AIN_LIST_MAX,
    AIN_RATE_MAX,
    AIN_RATE_MIN,
    AIN_REPEAT_MAX,
    DMA_PATTERN_MAX,
    DMA_RATE_MAX,
    DMA_WAVE_DEFAULT_MS,
    DMA_WAVE_MAX_MS,
    burst_seconds,
)
from .base import FakeGroup, _choice, _fail, _hex, _number, _shape

_INDEX_MAX = 0xFFFF
# `AdcTriggeredByTimerWithDma` paces its conversions with TIM2; TIM2's update request feeds `dma.wave`.
_PACING_TIMER = 2
_ADC_DMA = ("dma1", 7)
_WAVE_DMA = {"stm32wb55": ("dma2", 4), "stm32wba55": ("dma1", 8), "stm32g474": ("dma2", 4)}
_OUTPUTS = ("list", "stats")
# What the fake temperature sensor reads: a code and 25 degrees Celsius.
TEMPERATURE_CODE = 940
TEMPERATURE_MCELSIUS = 25_000


@dataclass
class _Burst:
    adc: int
    code: int
    samples: int
    seconds: float
    repeat: int
    output: str
    started: float
    reported: int = 0


class FakeAnalogInput(FakeGroup):
    prefix = "ain"
    owner: ClassVar[tuple[str, str]] = ("ain", "ain")

    def boot(self) -> None:
        self.burst: _Burst | None = None

    def _index(self, text: str) -> int:
        return _number(text, 0, _INDEX_MAX)

    def _analog_pin(self, text: str) -> str:
        """`ParseAnalogPin`: a name of a pin with an ADC channel, else `ERR pin`."""
        pin = self.fw.pin(text)
        assert pin is not None
        if not self.fw.supports_analog(pin):
            _fail("pin")
        return pin

    def _claim(self, pin: str | None, timed: bool) -> None:
        """`Claim`: the ADC, with `timed` its DMA channel and TIM2, and the pin as an analog input (all `busy`)."""
        resources = [("adc", self.fw.spec.adc)] + ([_ADC_DMA] if timed else [])
        self.fw.check_resources(self.owner, resources)
        if timed and self.fw.timer_owners.get(_PACING_TIMER, self.owner) != self.owner:
            _fail("busy")
        self.fw.check_pins(self.owner, [pin], analog=True)
        for kind, index in resources:
            self.fw.claim_resource(kind, index, self.owner)
        if timed:
            self.fw.timer_owners[_PACING_TIMER] = self.owner
        self.fw.claim(self.owner, [pin], analog=True)

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, ("sampling",))
        adc = self._index(args[0])
        _choice(options, "sampling", self.fw.spec.sampling, self.fw.spec.sampling[0])
        if adc != self.fw.spec.adc:
            _fail("range")
        temperature = args[1] == "temp"
        pin = None if temperature else self._analog_pin(args[1])
        if self.burst is not None:
            _fail("busy")
        self._claim(pin, timed=False)
        self.fw.release(self.owner)
        if temperature:
            return f"OK code={TEMPERATURE_CODE} mcelsius={TEMPERATURE_MCELSIUS}"
        assert pin is not None
        return f"OK code={self.fw.adc_codes.get(pin, 2048)}"

    def cmd_burst(self, args: list[str], options: dict[str, str]) -> None:
        _shape(args, options, 2, 2, ("n", "rate", "repeat", "out"))
        if "n" not in options or "rate" not in options:
            _fail("usage")
        adc = self._index(args[0])
        samples = _number(options["n"], 1, AIN_BUFFER)
        rate = _number(options["rate"], AIN_RATE_MIN, AIN_RATE_MAX)
        repeat = _number(options.get("repeat", "1"), 1, AIN_REPEAT_MAX)
        output = _choice(options, "out", _OUTPUTS, "list")
        if adc != self.fw.spec.adc or (output == "list" and samples > AIN_LIST_MAX):
            _fail("range")
        pin = self._analog_pin(args[1])
        if self.burst is not None:
            _fail("busy")
        self._claim(pin, timed=True)
        seconds = burst_seconds(samples, self.fw.kernel_clock, rate)
        code = self.fw.adc_codes.get(pin, 2048)
        self.burst = _Burst(adc, code, samples, seconds, repeat, output, self.fw.clock())
        return None

    def _run_fields(self, burst: _Burst) -> str:
        us = round(burst.seconds * 1e6)
        if burst.output == "list":
            return f"samples={','.join([str(burst.code)] * burst.samples)} us={us}"
        return f"n={burst.samples} min={burst.code} max={burst.code} mean={burst.code} us={us}"

    def poll(self) -> None:
        burst = self.burst
        if burst is None:
            return
        while burst.reported < burst.repeat and self.fw.clock() >= burst.started + (burst.reported + 1) * burst.seconds:
            burst.reported += 1
            if burst.reported < burst.repeat:
                self.fw.event(f"EVT ain index={burst.adc} run={burst.reported} {self._run_fields(burst)}")
                continue
            self.burst = None
            self.fw.release(self.owner)
            self.fw.event(f"OK {self._run_fields(burst)}")


@dataclass
class _Wave:
    ends: float


class FakeDma(FakeGroup):
    prefix = "dma"
    owner: ClassVar[tuple[str, str]] = ("dma", "wave")

    def boot(self) -> None:
        self.wave: _Wave | None = None

    def cmd_wave(self, args: list[str], options: dict[str, str]) -> None:
        _shape(args, options, 1, 1, ("rate", "pattern", "ms"))
        if "rate" not in options or "pattern" not in options:
            _fail("usage")
        _number(options["rate"], 1, DMA_RATE_MAX)
        if not _hex(options["pattern"], DMA_PATTERN_MAX):
            _fail("usage")
        ms = _number(options.get("ms", str(DMA_WAVE_DEFAULT_MS)), 1, DMA_WAVE_MAX_MS)
        pin = self.fw.pin(args[0])
        assert pin is not None
        if not self.fw.bonded(pin):
            _fail("pin")
        if self.wave is not None:
            _fail("busy")
        resource = _WAVE_DMA[self.fw.family]
        self.fw.check_pins(self.owner, [pin])
        if self.fw.timer_owners.get(_PACING_TIMER, self.owner) != self.owner:
            _fail("busy")
        self.fw.check_resources(self.owner, [resource])
        self.fw.claim(self.owner, [pin])
        self.fw.timer_owners[_PACING_TIMER] = self.owner
        self.fw.claim_resource(*resource, self.owner)
        self.wave = _Wave(self.fw.clock() + ms / 1000)
        return None

    def poll(self) -> None:
        if self.wave is not None and self.fw.clock() >= self.wave.ends:
            self.wave = None
            self.fw.release(self.owner)
            self.fw.event("OK")
