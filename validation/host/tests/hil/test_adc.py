"""ADC (`hal::AdcStm` with `hal::AdcDmaMultiChannelStmBase`): wavegen DC levels against raw 12-bit codes.

Wiring set `bundle1`: W1/W2 on the two inputs of `tests.adc.inputs`; the scope on the same pins measures the actual
level, which then replaces the programmed one as reference. Without `timer` each run is software triggered; with
`timer` the TRGO of that timer triggers the runs at `rate` per second.

Scenarios: features/adc.feature.
"""

from __future__ import annotations

import math
import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

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


@pytest.mark.matrix("adc.levels")
@scenario("adc.feature", "The ADC reads every DC level as its code")
def test_dc_levels(pin, level, trigger):
    pass


@pytest.mark.matrix("adc.sequence")
@scenario("adc.feature", "A sequence converts its pins in the given order")
def test_sequence(length, trigger):
    pass


@pytest.mark.matrix("adc.timing")
@scenario("adc.feature", "The ADC reads mid-supply as its code with every sampling time")
def test_sampling_time(sampling, trigger):
    pass


@pytest.mark.matrix("adc.trigger_rate")
@scenario("adc.feature", "Timer-triggered runs arrive at the rate")
def test_trigger_rate(timer, rate):
    pass


@scenario("adc.feature", "adc.measure gives up after 1000 ms")
def test_measure_timeout():
    pass


@scenario("adc.feature", "A timer that triggers the ADC is busy for PWM and the encoder")
def test_trigger_timer_busy_for_pwm_and_encoder():
    pass


@pytest.mark.board_params("timer", "adc.trigger.unsupported_timers")
@scenario("adc.feature", "The ADC refuses a timer that cannot trigger it")
def test_unsupported_trigger_timer(timer):
    pass


@scenario("adc.feature", "Malformed and out-of-range opens are refused")
def test_open_errors():
    pass


@scenario("adc.feature", "adc.measure needs an open ADC, one ADC is open at a time, and the run count is limited")
def test_measure_limits():
    pass


@scenario("adc.feature", "A GPIO on the pin blocks the ADC")
def test_gpio_pin_blocks_adc():
    pass


@scenario("adc.feature", "An ADC pin blocks a GPIO")
def test_adc_pin_blocks_gpio():
    pass


@given("the pin is driven to the level, measured by the scope where it is wired", target_fixture="volts")
def pin_at_level(ad3, need, pin, level):
    return apply_level(ad3, need, pin, level)


@given(parsers.parse("the first input is driven to {programmed:g} V, measured by the scope where it is wired"), target_fixture="volts")
def first_input_at(ad3, need, adc_cfg, programmed):
    return apply_level(ad3, need, adc_cfg["inputs"][0], programmed)


@given(
    parsers.parse("the first input is driven to {low:g} V and the second to {high:g} V, each measured by the scope where it is wired"),
    target_fixture="levels",
)
def inputs_at(ad3, need, adc_cfg, low, high):
    first, second = adc_cfg["inputs"]
    return {first: apply_level(ad3, need, first, low), second: apply_level(ad3, need, second, high)}


@given("a sequence of the length alternates the two inputs and takes the next spare input every third position", target_fixture="sequence")
def sequence_of_length(adc_cfg, length):
    first, second = adc_cfg["inputs"]
    return sequence_pins((first, second), adc_cfg.get("spare_inputs", []), length)


@given(
    "the run count of the board file, capped at what fits for one pin and at half a second of runs at the rate (at least 2)",
    target_fixture="runs",
)
def run_count(adc_cfg, rate):
    return min(adc_cfg["trigger"]["runs"], expect.adc_max_runs(1), max(2, int(rate * 0.5)))


@given("the first encoder of the board file on a timer that can trigger the ADC", target_fixture="encoder")
def trigger_encoder(board_cfg):
    return next(instance for instance in board_cfg.param("qei.instances") if instance["index"] in expect.ADC_TRIGGER_TIMERS)


@when("the ADC is opened on the pin with the trigger")
def open_on_pin(fw, adc_cfg, pin, trigger):
    fw.adc.open(adc_cfg["adc"], pins=[pin], **trigger_options(adc_cfg, trigger))


@when("the ADC is opened on the sequence with the trigger")
def open_on_sequence(fw, adc_cfg, sequence, trigger):
    fw.adc.open(adc_cfg["adc"], pins=sequence, **trigger_options(adc_cfg, trigger))


@when("the ADC is opened on the first input with the sampling time and the trigger")
def open_with_sampling(fw, adc_cfg, sampling, trigger):
    fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]], sampling=sampling, **trigger_options(adc_cfg, trigger))


@when("the ADC is opened on the first input, triggered by the timer at the rate")
def open_triggered(fw, adc_cfg, timer, rate):
    fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]], timer=timer, rate=rate)


@when(parsers.parse("the ADC is opened on the first input, triggered by the trigger timer at {per_second:d} run per second"))
def open_slow(fw, adc_cfg, per_second):
    fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]], timer=adc_cfg["trigger"]["timer"], rate=per_second)


@when("the ADC is opened on the first input, triggered by the timer of the encoder")
def open_on_encoder_timer(fw, adc_cfg, encoder):
    fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]], timer=encoder["index"])


@when("the ADC is opened on the inputs")
def open_on_inputs(fw, adc_cfg):
    fw.adc.open(adc_cfg["adc"], pins=adc_cfg["inputs"])


@when("the ADC is opened on a sequence of the first input twice")
def open_on_repeated_input(fw, adc_cfg):
    pin = adc_cfg["inputs"][0]
    fw.adc.open(adc_cfg["adc"], pins=[pin, pin])


@when("the first input is configured as a GPIO input")
def gpio_input(fw, adc_cfg):
    fw.gpio.cfg(adc_cfg["inputs"][0], "in")


@when("the ADC measures the sample count of runs, at most as many as fit for the length", target_fixture="measurement")
def measure_sequence(fw, adc_cfg, length):
    runs = min(adc_cfg["samples"], expect.adc_max_runs(length))
    return runs, fw.adc.measure(adc_cfg["adc"], n=runs)


@when("a single run is timed: the shortest round trip of the repeats less the transmission of its samples", target_fixture="single")
def time_single(fw, board_cfg, adc_cfg):
    return timed_measure(fw, adc_cfg["adc"], 1, adc_cfg["trigger"]["repeats"], board_cfg.terminal.baud)


@when("the runs are timed the same way, less the single run", target_fixture="measured")
def time_runs(fw, board_cfg, adc_cfg, runs, single):
    return timed_measure(fw, adc_cfg["adc"], runs, adc_cfg["trigger"]["repeats"], board_cfg.terminal.baud) - single


@then("the samples, as many as the sample count, read the level as its code within the tolerance and the spread")
def samples_read_level(fw, adc_cfg, volts):
    check_codes(fw.adc.measure(adc_cfg["adc"], n=adc_cfg["samples"]), volts, adc_cfg)


@then("there is one sample per pin of the sequence in every run")
def one_sample_per_pin(length, measurement):
    runs, samples = measurement
    assert len(samples) == runs * length


@then("every driven pin of the sequence reads its level as its code within the tolerance and the spread")
def driven_pins_read_levels(adc_cfg, length, levels, sequence, measurement):
    _, samples = measurement
    for pin, channel in zip(sequence, analysis.deinterleave(samples, length)):
        if pin in levels:
            check_codes(channel, levels[pin], adc_cfg)


@then("the extra runs took their time at the rate within the rate tolerance and the jitter")
def runs_at_rate(adc_cfg, rate, runs, measured):
    settings = adc_cfg["trigger"]
    expected = expect.adc_measure_time(runs, rate) - expect.adc_measure_time(1, rate)
    tolerance, jitter = settings["rate_tolerance"], settings["jitter_s"]
    assert (1 - tolerance) * expected - jitter <= measured <= (1 + tolerance) * expected + jitter, (
        f"{runs - 1} more runs took {measured * 1000:.2f} ms, {expected * 1000:.2f} ms at {rate} Hz"
    )


@then(parsers.parse('measuring {count:d} runs with a {seconds:g} s command timeout fails with "{reason}"'))
def measure_refused(fw, adc_cfg, count, seconds, reason):
    with pytest.raises(FirmwareError) as error:
        fw.adc.measure(adc_cfg["adc"], n=count, cmd_timeout=seconds)
    assert error.value.reason == reason


@then(parsers.parse('opening PWM on that timer with channel {channel:d} fails with "{reason}"'))
def pwm_refused(fw, encoder, channel, reason):
    with pytest.raises(FirmwareError) as error:
        fw.pwm.open(encoder["index"], channels=[channel])
    assert error.value.reason == reason, "PWM"


@then(parsers.parse('opening the encoder fails with "{reason}"'))
def encoder_refused(fw, encoder, reason):
    with pytest.raises(FirmwareError) as error:
        fw.qei.open(encoder["index"], a=encoder["a"], b=encoder["b"])
    assert error.value.reason == reason, "encoder"


@then(parsers.parse('opening the ADC on the first input, triggered by the timer, fails with "{reason}"'))
def trigger_timer_refused(fw, adc_cfg, timer, reason):
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]], timer=timer)
    assert error.value.reason == reason


@then("every malformed or out-of-range open of the ADC on the first input fails with its reason")
def open_errors(fw, board_cfg, adc_cfg):
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


@then(parsers.parse('opening another ADC number on the first input fails with "{reason}"'))
def other_adc_refused(fw, adc_cfg, reason):
    adc, pin = adc_cfg["adc"], adc_cfg["inputs"][0]
    missing = next(number for number in range(8) if number != adc)
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(missing, pins=[pin])
    assert error.value.reason == reason, "another ADC number"


@then(parsers.parse('measuring the ADC fails with "{reason}"'))
def measure_unopened(fw, adc_cfg, reason):
    with pytest.raises(FirmwareError) as error:
        fw.adc.measure(adc_cfg["adc"])
    assert error.value.reason == reason


@then(parsers.parse('opening the ADC on the inputs again fails with "{reason}"'))
def second_open_refused(fw, adc_cfg, reason):
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(adc_cfg["adc"], pins=adc_cfg["inputs"])
    assert error.value.reason == reason, "one ADC at a time"


@then("measuring as many runs as fit gives one sample per input in every run")
def largest_measure(fw, adc_cfg):
    pins = adc_cfg["inputs"]
    largest = expect.adc_max_runs(len(pins))
    assert len(fw.adc.measure(adc_cfg["adc"], n=largest)) == largest * len(pins)


@then(parsers.parse('measuring 0 runs, one more than fit or more than the value limit fails with "{reason}"'))
def run_counts_refused(fw, adc_cfg, reason):
    largest = expect.adc_max_runs(len(adc_cfg["inputs"]))
    for runs in (0, largest + 1, expect.ADC_MAX_VALUES + 1):
        with pytest.raises(FirmwareError) as error:
            fw.adc.measure(adc_cfg["adc"], n=runs)
        assert error.value.reason == reason, runs


@then(parsers.parse('opening the ADC on the first input fails with "{reason}"'))
def open_refused(fw, adc_cfg, reason):
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(adc_cfg["adc"], pins=[adc_cfg["inputs"][0]])
    assert error.value.reason == reason


@then(parsers.parse('configuring the first input as a GPIO input fails with "{reason}"'))
def gpio_refused(fw, adc_cfg, reason):
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(adc_cfg["inputs"][0], "in")
    assert error.value.reason == reason
