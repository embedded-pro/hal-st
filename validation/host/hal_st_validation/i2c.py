"""I2C bus helpers for the `i2c`, `i2cs` and `eeprom` groups of validation/PROTOCOL.md: decoding a logic-analyzer
capture of SCL and SDA, the TIMINGR arithmetic of validation/firmware/I2cTiming.cpp (`expected_timing`, bit for bit),
and AD3 helpers that start on a START condition (a logic-analyzer capture, or an SDA pulse for arbitration loss).

The AD3 helpers pass every argument as a ctypes value, like `ad3_waveforms_bench.instruments.ad3`, so they also work
through `--ad3-remote`.
"""

from __future__ import annotations

import time
from collections.abc import Sequence
from ctypes import byref, c_double, c_int, c_ubyte, c_uint
from dataclasses import dataclass, field
from statistics import median
from typing import TYPE_CHECKING, Any, NamedTuple

from ad3_waveforms_bench.instruments.ad3 import PendingCapture

if TYPE_CHECKING:
    from ad3_waveforms_bench import AnalogDiscovery3

__all__ = [
    "BUS_MAX",
    "BUS_MIN",
    "FAST_MODE",
    "STANDARD_MODE",
    "I2cByte",
    "I2cTransfer",
    "ModeLimits",
    "PendingScope",
    "SclTiming",
    "arbitration_window",
    "arm_on_start",
    "arm_scope",
    "bit_times",
    "expected_timing",
    "i2c_decode",
    "inject_on_start",
    "mode_limits",
    "rise_times",
    "scl_timing",
    "timing_fields",
]

BUS_MIN = 20_000
BUS_MAX = 400_000
_PICOSECONDS = 10**12
_FIELD_MAX = 15
_PERIOD_MAX = 255
_ANALOG_FILTER_MIN = 50_000
_ANALOG_FILTER_MAX = 260_000


@dataclass(frozen=True)
class ModeLimits:
    """I2C specification limits of one speed mode, in picoseconds (`rise`/`fall` are the characterisation edges
    the TIMINGR computation assumes)."""

    hddat_min: int
    vddat_max: int
    sudat_min: int
    lscl_min: int
    hscl_min: int
    rise: int
    fall: int

    @property
    def tlow_min(self) -> float:
        return self.lscl_min / _PICOSECONDS

    @property
    def thigh_min(self) -> float:
        return self.hscl_min / _PICOSECONDS


STANDARD_MODE = ModeLimits(0, 3_450_000, 250_000, 4_700_000, 4_000_000, 640_000, 20_000)
FAST_MODE = ModeLimits(0, 900_000, 100_000, 1_300_000, 600_000, 250_000, 100_000)


def mode_limits(bus_hz: int) -> ModeLimits:
    """Standard mode up to 100 kHz, Fast mode above."""
    return STANDARD_MODE if bus_hz <= 100_000 else FAST_MODE


def _ceil_div(numerator: int, denominator: int) -> int:
    return -(-numerator // denominator)


def expected_timing(kernel_hz: int, bus_hz: int) -> int | None:
    """TIMINGR for an I2C kernel clock and bus frequency, as `I2cTiming` computes it; None outside 20..400 kHz.

    Integer picoseconds: the first SCLDEL/SDADEL pair per prescaler, the minimal SCLL/SCLH, then the fewest clock
    units whose period with a zero rise time is not shorter than 1/bus (extra units go to SCLL first); the shortest
    period wins, ties to the smaller prescaler."""
    if kernel_hz <= 0 or not BUS_MIN <= bus_hz <= BUS_MAX:
        return None
    mode = mode_limits(bus_hz)
    clock = _PICOSECONDS // kernel_hz
    target = _PICOSECONDS // bus_hz
    sdadel_min = max(mode.fall + mode.hddat_min - _ANALOG_FILTER_MIN - 3 * clock, 0)
    sdadel_max = max(mode.vddat_max - mode.rise - _ANALOG_FILTER_MAX - 4 * clock, 0)
    scldel_min = mode.rise + mode.sudat_min
    best: tuple[int, int, int, int, int, int] | None = None
    for prescaler in range(_FIELD_MAX + 1):
        prescaled = (prescaler + 1) * clock
        scldel = next((value for value in range(_FIELD_MAX + 1) if (value + 1) * prescaled >= scldel_min), None)
        if scldel is None:
            continue
        sdadel = next(
            (value for value in range(_FIELD_MAX + 1) if sdadel_min <= (value * (prescaler + 1) + 1) * clock <= sdadel_max),
            None,
        )
        if sdadel is None:
            continue
        sync = _ANALOG_FILTER_MIN + 2 * clock
        scll_min = max(0, (mode.lscl_min - sync) // prescaled)
        sclh_min = max(0, _ceil_div(mode.hscl_min - sync, prescaled) - 1)
        fixed = 2 * sync + mode.fall
        units = max(scll_min + sclh_min + 2, _ceil_div(target - fixed, prescaled))
        extra = units - (scll_min + sclh_min + 2)
        scll = scll_min + (extra + 1) // 2
        sclh = sclh_min + extra // 2
        if scll > _PERIOD_MAX or sclh > _PERIOD_MAX:
            continue
        period = fixed + units * prescaled
        if best is None or period < best[0]:
            best = (period, prescaler, scldel, sdadel, sclh, scll)
    if best is None:
        return None
    _, prescaler, scldel, sdadel, sclh, scll = best
    return prescaler << 28 | scldel << 20 | sdadel << 16 | sclh << 8 | scll


def timing_fields(timingr: int) -> dict[str, int]:
    """The fields of a TIMINGR value: `presc`, `scldel`, `sdadel`, `sclh`, `scll`."""
    return {
        "presc": timingr >> 28 & 0xF,
        "scldel": timingr >> 20 & 0xF,
        "sdadel": timingr >> 16 & 0xF,
        "sclh": timingr >> 8 & 0xFF,
        "scll": timingr & 0xFF,
    }


def bit_times(timingr: int, kernel_hz: int) -> tuple[float, float]:
    """(tLOW, tHIGH) in seconds of a master running `timingr` at `kernel_hz` on a bus with zero rise time: each is
    (SCLx + 1) prescaled clocks plus the analog filter and the two-clock synchronisation (ST's timing model)."""
    fields = timing_fields(timingr)
    clock = 1 / kernel_hz
    prescaled = (fields["presc"] + 1) * clock
    sync = _ANALOG_FILTER_MIN / _PICOSECONDS + 2 * clock
    return sync + (fields["scll"] + 1) * prescaled, sync + (fields["sclh"] + 1) * prescaled


def arbitration_window(timingr: int, kernel_hz: int) -> tuple[float, float]:
    """(delay, width) in seconds of an SDA pulse that starts in the middle of the SCL low phase of the second address
    bit after a START and lasts one SCL period, so it covers that bit's high phase: the START hold is one tHIGH, then
    bit 1, then half of bit 2's tLOW."""
    low, high = bit_times(timingr, kernel_hz)
    return 2 * high + 1.5 * low, low + high


@dataclass(frozen=True)
class I2cByte:
    value: int
    ack: bool


@dataclass
class I2cTransfer:
    """One address phase and its data bytes, from a START (`restart` when no STOP preceded it) to the next START or
    STOP; `start` and `end` are sample indices (the START, and the STOP or the last sampled bit)."""

    restart: bool
    address: int
    read: bool
    address_ack: bool
    start: int = 0
    end: int = 0
    data: list[I2cByte] = field(default_factory=list)
    stop: bool = False

    @property
    def payload(self) -> bytes:
        return bytes(byte.value for byte in self.data)


def i2c_decode(scl: Sequence[int], sda: Sequence[int]) -> list[I2cTransfer]:
    """START/repeated START: SDA falls while SCL stays high; STOP: SDA rises while SCL stays high; a bit is the SDA
    level at the first sample of each SCL high phase, nine bits per byte with the acknowledge last (0 = ACK)."""
    transfers: list[I2cTransfer] = []
    bits: list[int] = []
    current: I2cTransfer | None = None
    in_frame = False
    restart = False
    started = 0
    for index in range(1, min(len(scl), len(sda))):
        if scl[index] and scl[index - 1] and sda[index] != sda[index - 1]:
            if current is not None:
                current.end = index
                current.stop = bool(sda[index])
                transfers.append(current)
            if not sda[index]:
                restart = in_frame
                started = index
                in_frame = True
            else:
                in_frame = False
            current, bits = None, []
            continue
        if in_frame and scl[index] and not scl[index - 1]:
            bits.append(1 if sda[index] else 0)
            if len(bits) == 9:
                value = int("".join(map(str, bits[:8])), 2)
                ack = bits[8] == 0
                if current is None:
                    current = I2cTransfer(restart, value >> 1, bool(value & 1), ack, start=started)
                else:
                    current.data.append(I2cByte(value, ack))
                current.end = index
                bits = []
    if current is not None:
        transfers.append(current)
    return transfers


class SclTiming(NamedTuple):
    """`freq` from the median SCL period (rising edge to rising edge); `tlow_min`/`thigh_min` the shortest complete
    low and high phases, in seconds."""

    freq: float
    tlow_min: float
    thigh_min: float


def scl_timing(scl: Sequence[int], rate: float) -> SclTiming:
    """SCL clock figures of a capture sampled at `rate`; only phases bounded by edges on both sides count, so the
    idle level before the first and after the last edge is ignored."""
    edges = [index for index in range(1, len(scl)) if scl[index] != scl[index - 1]]
    lows = [(edges[i + 1] - edges[i]) / rate for i in range(len(edges) - 1) if not scl[edges[i]]]
    highs = [(edges[i + 1] - edges[i]) / rate for i in range(len(edges) - 1) if scl[edges[i]]]
    rising = [index for index in edges if scl[index]]
    periods = [(rising[i + 1] - rising[i]) / rate for i in range(len(rising) - 1)]
    if not periods or not lows or not highs:
        raise ValueError("fewer than two SCL clock periods in the capture")
    return SclTiming(1 / median(periods), min(lows), min(highs))


def _wait(condition: Any, timeout: float, what: str) -> None:
    deadline = time.monotonic() + timeout
    while not condition():
        if time.monotonic() > deadline:
            raise TimeoutError(f"{what} timed out after {timeout} s")
        time.sleep(0.001)


def _detect_start(ad3: AnalogDiscovery3, scl_dio: int, sda_dio: int) -> None:
    """The DigitalIn trigger detector on a START: SDA falling while SCL is high."""
    ad3.api.FDwfDigitalInTriggerSet(ad3.handle, c_uint(0), c_uint(1 << scl_dio), c_uint(0), c_uint(1 << sda_dio))


def arm_on_start(
    ad3: AnalogDiscovery3, scl_dio: int, sda_dio: int, rate: float, samples: int, pretrigger: float = 0.1, arm_timeout: float = 2.0
) -> PendingCapture:
    """`LogicAnalyzer.arm` with the trigger on a START condition instead of one edge; returns once armed, so a
    transfer started afterwards cannot be missed. `wait()` the result for the capture."""
    analyzer = ad3.logic
    buffer = analyzer.buffer_size
    if samples > buffer:
        raise ValueError(f"{samples} samples exceed the logic analyzer buffer ({buffer})")
    api, handle, constants = ad3.api, ad3.handle, ad3.api.constants
    divider = max(1, round(analyzer.clock_hz / rate))
    before = int(samples * pretrigger)
    api.FDwfDigitalInReset(handle)
    api.FDwfDigitalInAcquisitionModeSet(handle, constants.acqmodeSingle)
    api.FDwfDigitalInDividerSet(handle, c_uint(divider))
    api.FDwfDigitalInSampleFormatSet(handle, c_int(16))
    api.FDwfDigitalInBufferSizeSet(handle, c_int(samples))
    api.FDwfDigitalInTriggerPositionSet(handle, c_uint(samples - before))
    api.FDwfDigitalInTriggerSourceSet(handle, constants.trigsrcDetectorDigitalIn)
    _detect_start(ad3, scl_dio, sda_dio)
    api.FDwfDigitalInConfigure(handle, c_int(1), c_int(1))
    waiting = {int(getattr(state, "value", state)) for state in (constants.DwfStateConfig, constants.DwfStatePrefill)}

    def armed() -> bool:
        state = c_ubyte()
        api.FDwfDigitalInStatus(handle, c_int(0), byref(state))
        return int(state.value) not in waiting

    _wait(armed, arm_timeout, "logic analyzer arm")
    return PendingCapture(analyzer, analyzer.clock_hz / divider, samples, before)


def inject_on_start(ad3: AnalogDiscovery3, scl_dio: int, sda_dio: int, delay_s: float, width_s: float) -> None:
    """Arm one open-drain low pulse on SDA, `width_s` long, `delay_s` after the next START (the DigitalIn trigger
    detector starts the pattern generator): a master sending a 1 during the pulse loses arbitration. SDA is released
    (high impedance) before and after the pulse; `ad3.pattern.stop()` disarms it."""
    if delay_s < 0 or width_s <= 0:
        raise ValueError("delay must not be negative and width must be positive")
    api, handle, constants = ad3.api, ad3.handle, ad3.api.constants
    clock = ad3.pattern.clock_hz
    low, high = c_uint(), c_uint()
    api.FDwfDigitalOutCounterInfo(handle, c_int(sda_dio), byref(low), byref(high))
    counter_max = int(high.value) or 0x7FFF
    divider = max(1, -(-round(width_s * clock) // counter_max))
    ticks = max(1, round(width_s * clock / divider))
    api.FDwfDigitalOutReset(handle)
    _detect_start(ad3, scl_dio, sda_dio)
    api.FDwfDigitalInConfigure(handle, c_int(1), c_int(0))
    api.FDwfDigitalOutEnableSet(handle, c_int(sda_dio), c_int(1))
    api.FDwfDigitalOutOutputSet(handle, c_int(sda_dio), constants.DwfDigitalOutOutputOpenDrain)
    api.FDwfDigitalOutTypeSet(handle, c_int(sda_dio), constants.DwfDigitalOutTypePulse)
    api.FDwfDigitalOutIdleSet(handle, c_int(sda_dio), constants.DwfDigitalOutIdleZet)
    api.FDwfDigitalOutDividerSet(handle, c_int(sda_dio), c_uint(divider))
    api.FDwfDigitalOutCounterSet(handle, c_int(sda_dio), c_uint(ticks), c_uint(ticks))
    api.FDwfDigitalOutCounterInitSet(handle, c_int(sda_dio), c_int(0), c_uint(ticks))
    api.FDwfDigitalOutTriggerSourceSet(handle, constants.trigsrcDetectorDigitalIn)
    api.FDwfDigitalOutWaitSet(handle, c_double(delay_s))
    api.FDwfDigitalOutRunSet(handle, c_double(width_s))
    api.FDwfDigitalOutRepeatSet(handle, c_uint(1))
    api.FDwfDigitalOutConfigure(handle, c_int(1))


def rise_times(samples: Sequence[float], rate: float, low: float, high: float) -> list[float]:
    """The `low` to `high` crossing times (seconds) of every rising edge of an analog capture, interpolated
    linearly between samples; an edge still rising at the end of the capture is left out."""

    def crossing(index: int, level: float) -> float:
        before, after = samples[index - 1], samples[index]
        return index - 1 + (level - before) / (after - before)

    times: list[float] = []
    start: float | None = None
    for index in range(1, len(samples)):
        if samples[index - 1] < low <= samples[index]:
            start = crossing(index, low)
        if start is not None and samples[index - 1] < high <= samples[index]:
            times.append((crossing(index, high) - start) / rate)
            start = None
        elif samples[index] < low:
            start = None
    return times


@dataclass
class PendingScope:
    """An armed scope acquisition (`arm_scope`); `wait()` returns the samples (volts) per scope channel."""

    ad3: AnalogDiscovery3
    channels: tuple[int, ...]
    samples: int

    def wait(self, timeout: float = 5.0) -> dict[int, list[float]]:
        api, handle, constants = self.ad3.api, self.ad3.handle, self.ad3.api.constants
        done = int(getattr(constants.DwfStateDone, "value", constants.DwfStateDone))

        def finished() -> bool:
            state = c_ubyte()
            api.FDwfAnalogInStatus(handle, c_int(1), byref(state))
            return int(state.value) == done

        _wait(finished, timeout, "scope acquisition")
        result: dict[int, list[float]] = {}
        for channel in self.channels:
            buffer = (c_double * self.samples)()
            api.FDwfAnalogInStatusData(handle, c_int(channel - 1), byref(buffer), c_int(self.samples))
            result[channel] = list(buffer)
        return result


def arm_scope(
    ad3: AnalogDiscovery3,
    channels: Sequence[int],
    rate: float,
    samples: int,
    trigger: int,
    level: float,
    range_v: float = 10.0,
    arm_timeout: float = 2.0,
) -> PendingScope:
    """A single scope acquisition of `channels` (1/2) that starts on the next rising edge of channel `trigger`
    through `level` volts, with the trigger at 10 % of the buffer and no auto trigger; returns once armed."""
    api, handle, constants = ad3.api, ad3.handle, ad3.api.constants
    for index in (0, 1):
        api.FDwfAnalogInChannelEnableSet(handle, c_int(index), c_int(int(index + 1 in channels)))
    for channel in channels:
        api.FDwfAnalogInChannelRangeSet(handle, c_int(channel - 1), c_double(range_v))
        api.FDwfAnalogInChannelOffsetSet(handle, c_int(channel - 1), c_double(0.0))
    api.FDwfAnalogInAcquisitionModeSet(handle, constants.acqmodeSingle)
    api.FDwfAnalogInFrequencySet(handle, c_double(rate))
    api.FDwfAnalogInBufferSizeSet(handle, c_int(samples))
    api.FDwfAnalogInTriggerAutoTimeoutSet(handle, c_double(0.0))
    api.FDwfAnalogInTriggerSourceSet(handle, constants.trigsrcDetectorAnalogIn)
    api.FDwfAnalogInTriggerTypeSet(handle, getattr(constants, "trigtypeEdge", c_int(0)))
    api.FDwfAnalogInTriggerChannelSet(handle, c_int(trigger - 1))
    api.FDwfAnalogInTriggerLevelSet(handle, c_double(level))
    api.FDwfAnalogInTriggerConditionSet(handle, constants.DwfTriggerSlopeRise)
    api.FDwfAnalogInTriggerPositionSet(handle, c_double(0.4 * samples / rate))
    api.FDwfAnalogInConfigure(handle, c_int(1), c_int(1))
    waiting = {int(getattr(state, "value", state)) for state in (constants.DwfStateConfig, constants.DwfStatePrefill)}

    def armed() -> bool:
        state = c_ubyte()
        api.FDwfAnalogInStatus(handle, c_int(0), byref(state))
        return int(state.value) not in waiting

    _wait(armed, arm_timeout, "scope arm")
    return PendingScope(ad3, tuple(channels), samples)
