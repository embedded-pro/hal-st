"""ADC (`hal::AdcStm` with `hal::AdcDmaMultiChannelStmBase`): wavegen DC levels against raw 12-bit codes.

Wiring set `bundle1`: W1/W2 on the two inputs of `tests.adc.inputs`; the scope on the same pins measures the actual
level, which then replaces the programmed one as reference. Without `timer` each run is software triggered; with
`timer` the TRGO of that timer triggers the runs at `rate` per second.
"""

from __future__ import annotations

import math
import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import expect


@pytest.fixture
def adc_cfg(board_cfg):
    return board_cfg.param("adc")


def apply_level(ad3, need, pin, volts):
    """Drive `pin` to `volts`; returns the measured level (scope) or the programmed one.

    The scope reads on a 5 V range centred on mid-supply and averages 100 ms, whole periods of both 50 and 60 Hz,
    so mains hum does not move the reference.
    """
    ad3.wavegen.dc(need.wavegen(pin), volts)
    time.sleep(0.02)
    scope = need.optional_scope(pin)
    if scope is None:
        return volts
    return statistics.fmean(ad3.scope.acquire([scope], rate=5e4, samples=5000, range_v=5.0, offset_v=1.65)[scope])


def trigger_options(adc_cfg, trigger):
    if trigger == "software":
        return {}
    return {"timer": adc_cfg["trigger"]["timer"], "rate": adc_cfg["trigger"]["rate"]}


def check_codes(samples, volts, adc_cfg):
    expected = expect.adc_code(volts, adc_cfg["vref"], adc_cfg["bits"])
    result = analysis.stats(samples)
    assert result.mean == pytest.approx(expected, abs=adc_cfg["tolerance_codes"]), f"{volts:.3f} V: {result}"
    assert result.maximum - result.minimum <= adc_cfg["spread_codes"], f"noisy samples {result}"


@pytest.mark.ad3
@pytest.mark.matrix("adc.levels")
def test_dc_levels(fw, ad3, need, adc_cfg, pin, level, trigger):
    volts = apply_level(ad3, need, pin, level)
    fw.adc.open(adc_cfg["adc"], pins=[pin], **trigger_options(adc_cfg, trigger))
    check_codes(fw.adc.measure(adc_cfg["adc"], n=adc_cfg["samples"]), volts, adc_cfg)


def sequence_pins(driven, spares, length):
    """`length` conversions: the driven inputs alternate, and every third position takes the next spare input, so a
    sequence holds more distinct channels than the two driven ones."""
    pins = []
    for step in range(length):
        if step % 3 == 2 and spares:
            pins.append(spares[(step // 3) % len(spares)])
        else:
            pins.append(driven[(step - step // 3) % len(driven)])
    return pins


@pytest.mark.ad3
@pytest.mark.matrix("adc.sequence")
def test_sequence(fw, ad3, need, adc_cfg, length, trigger):
    """One conversion per pin in the given order: the driven inputs alternate, interleaved with unwired spare inputs
    whose codes are not checked."""
    first, second = adc_cfg["inputs"]
    levels = {first: apply_level(ad3, need, first, 0.8), second: apply_level(ad3, need, second, 2.4)}
    pins = sequence_pins((first, second), adc_cfg.get("spare_inputs", []), length)
    fw.adc.open(adc_cfg["adc"], pins=pins, **trigger_options(adc_cfg, trigger))
    runs = min(adc_cfg["samples"], expect.adc_max_runs(length))
    samples = fw.adc.measure(adc_cfg["adc"], n=runs)
    assert len(samples) == runs * length
    for pin, channel in zip(pins, analysis.deinterleave(samples, length)):
        if pin in levels:
            check_codes(channel, levels[pin], adc_cfg)


@pytest.mark.ad3
@pytest.mark.matrix("adc.timing")
def test_sampling_time(fw, ad3, need, adc_cfg, sampling, trigger):
    pin = adc_cfg["inputs"][0]
    volts = apply_level(ad3, need, pin, 1.65)
    fw.adc.open(adc_cfg["adc"], pins=[pin], sampling=sampling, **trigger_options(adc_cfg, trigger))
    check_codes(fw.adc.measure(adc_cfg["adc"], n=adc_cfg["samples"]), volts, adc_cfg)


def timed_measure(fw, adc, runs, repeats, baud):
    """Shortest of `repeats` round trips of `adc.measure n=<runs>` (the minimum drops the scheduling jitter of the
    host and the link), less the transmission of the reply's samples."""
    shortest = math.inf
    for _ in range(repeats):
        start = time.monotonic()
        samples = fw.adc.measure(adc, n=runs, cmd_timeout=3.0)
        shortest = min(shortest, time.monotonic() - start)
        assert len(samples) == runs
    return shortest - expect.uart_transfer_time(len(",".join(str(sample) for sample in samples)), baud)


@pytest.mark.matrix("adc.trigger_rate")
def test_trigger_rate(fw, board_cfg, adc_cfg, timer, rate):
    """Timer-triggered runs arrive at `rate` per second: `adc.measure n=<runs>` takes (runs - 1) / rate longer than
    `n=1` (`expect.adc_measure_time`), and the difference cancels the command round trip."""
    settings = adc_cfg["trigger"]
    adc, baud = adc_cfg["adc"], board_cfg.terminal.baud
    runs = min(settings["runs"], expect.adc_max_runs(1), max(2, int(rate * 0.5)))
    fw.adc.open(adc, pins=[adc_cfg["inputs"][0]], timer=timer, rate=rate)
    single = timed_measure(fw, adc, 1, settings["repeats"], baud)
    measured = timed_measure(fw, adc, runs, settings["repeats"], baud) - single
    expected = expect.adc_measure_time(runs, rate) - expect.adc_measure_time(1, rate)
    tolerance, jitter = settings["rate_tolerance"], settings["jitter_s"]
    assert (1 - tolerance) * expected - jitter <= measured <= (1 + tolerance) * expected + jitter, (
        f"{runs - 1} more runs took {measured * 1000:.2f} ms, {expected * 1000:.2f} ms at {rate} Hz"
    )


def test_measure_timeout(fw, adc_cfg):
    """`adc.measure` gives up after 1000 ms: two runs at one per second answer `ERR timeout`."""
    adc = adc_cfg["adc"]
    fw.adc.open(adc, pins=[adc_cfg["inputs"][0]], timer=adc_cfg["trigger"]["timer"], rate=1)
    with pytest.raises(FirmwareError) as error:
        fw.adc.measure(adc, n=2, cmd_timeout=3.0)
    assert error.value.reason == "timeout"


def test_trigger_timer_busy_for_pwm_and_encoder(fw, board_cfg, adc_cfg):
    """A timer serves one group at a time: while it triggers the ADC, PWM and the encoder get `ERR busy`."""
    encoder = next(instance for instance in board_cfg.param("qei.instances") if instance["index"] in expect.ADC_TRIGGER_TIMERS)
    timer = encoder["index"]
    fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]], timer=timer)
    with pytest.raises(FirmwareError) as error:
        fw.pwm.open(timer, channels=[3])
    assert error.value.reason == "busy", "PWM"
    with pytest.raises(FirmwareError) as error:
        fw.qei.open(timer, a=encoder["a"], b=encoder["b"])
    assert error.value.reason == "busy", "encoder"


@pytest.mark.board_params("timer", "adc.trigger.unsupported_timers")
def test_unsupported_trigger_timer(fw, adc_cfg, timer):
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]], timer=timer)
    assert error.value.reason == "unsupported"


def test_open_errors(fw, board_cfg, adc_cfg):
    adc, pin = adc_cfg["adc"], adc_cfg["inputs"][0]
    sampling = expect.ADC_SAMPLING_TIMES[board_cfg.family]
    other_family = next(times for family, times in expect.ADC_SAMPLING_TIMES.items() if family != board_cfg.family)
    cases = [
        ({}, "usage"),
        ({"pins": [pin], "rate": 1000}, "usage"),
        ({"pins": [pin], "sampling": next(time for time in other_family if time not in sampling)}, "usage"),
        ({"pins": [pin] * (expect.ADC_MAX_PINS + 1)}, "range"),
        ({"pins": [pin], "timer": 2, "rate": 0}, "range"),
        ({"pins": [pin], "timer": 2, "rate": expect.ADC_RATE_MAX + 1}, "range"),
        ({"pins": [pin], "timer": 18}, "range"),
        ({"pins": [pin, board_cfg.param("gpio.output_pins")[0]]}, "pin"),
        *[({"pins": [pin], "timer": timer}, "range") for timer in adc_cfg["trigger"]["missing_timers"]],
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.adc.open(adc, **options)
        assert error.value.reason == reason, options
    missing = next(number for number in range(8) if number != adc)
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(missing, pins=[pin])
    assert error.value.reason == "range", "another ADC number"


def test_measure_limits(fw, adc_cfg):
    adc = adc_cfg["adc"]
    with pytest.raises(FirmwareError) as error:
        fw.adc.measure(adc)
    assert error.value.reason == "notopen"
    pins = adc_cfg["inputs"]
    fw.adc.open(adc, pins=pins)
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(adc, pins=pins)
    assert error.value.reason == "busy", "one ADC at a time"
    largest = expect.adc_max_runs(len(pins))
    assert len(fw.adc.measure(adc, n=largest)) == largest * len(pins)
    for runs in (0, largest + 1, expect.ADC_MAX_VALUES + 1):
        with pytest.raises(FirmwareError) as error:
            fw.adc.measure(adc, n=runs)
        assert error.value.reason == "range", runs


def test_gpio_pin_blocks_adc(fw, adc_cfg):
    pin = adc_cfg["inputs"][0]
    fw.gpio.cfg(pin, "in")
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(adc_cfg["adc"], pins=[pin])
    assert error.value.reason == "busy"


def test_adc_pin_blocks_gpio(fw, adc_cfg):
    """Analog users share a pin (a sequence may repeat it); a GPIO cannot take it."""
    pin = adc_cfg["inputs"][0]
    fw.adc.open(adc_cfg["adc"], pins=[pin, pin])
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(pin, "in")
    assert error.value.reason == "busy"
