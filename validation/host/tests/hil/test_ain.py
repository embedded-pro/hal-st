"""Analog input (`hal::AnalogToDigitalPinImplStm`, `hal::AnalogToDigitalInternalTemperatureStm`,
`hal::AdcTriggeredByTimerWithDma`, PROTOCOL.md D.11): single conversions against wavegen levels, the temperature
sensor, and TIM2-paced bursts on one driver over a 256-sample buffer (B.12: `Measure(n)` delivers exactly n samples
in n / rate, also a second time on the same driver).

Wiring set `bundle1`: W1/W2 and the scope on `tests.ain.pins` (the ADC inputs of `tests.adc`); the burst counts,
the temperature and the argument checks need no AD3.

Scenarios: features/ain.feature.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect
from hal_st_validation.groups import analog

RAMP_RISE_FRACTION = 0.5


@pytest.fixture
def ain_cfg(board_cfg):
    return board_cfg.param("ain")


@pytest.fixture
def adc_cfg(board_cfg):
    return board_cfg.param("adc")


def apply_level(ad3, need, pin, volts):
    """Drive `pin` to `volts`; returns the level the scope measures (100 ms, whole mains periods), else `volts`."""
    ad3.wavegen.dc(need.wavegen(pin), volts)
    time.sleep(0.02)
    scope = need.optional_scope(pin)
    if scope is None:
        return volts
    return statistics.fmean(ad3.scope.acquire([scope], rate=5e4, samples=5000, range_v=5.0, offset_v=1.65)[scope])


def expected_code(volts, adc_cfg):
    return expect.adc_code(volts, adc_cfg["vref"], adc_cfg["bits"])


def check_timing(run, samples, board_cfg, ain_cfg):
    settings = ain_cfg["burst"]
    expected = analog.burst_seconds(samples, board_cfg.clock("timer"), settings["rate"])
    measured = run.us / 1e6
    assert abs(measured - expected) <= settings["tolerance"] * expected + settings["latency_s"], (
        f"{run.n} samples in {measured * 1000:.2f} ms, {samples} take {expected * 1000:.2f} ms"
    )


def ramp_span(ain_cfg, adc_cfg):
    settings = ain_cfg["ramp"]
    return expected_code(settings["high"], adc_cfg) - expected_code(settings["low"], adc_cfg)


@pytest.mark.board_params("pin", "ain.pins")
@pytest.mark.board_params("level", "ain.levels")
@scenario("ain.feature", "ain.read converts the pin once")
def test_read_dc(pin, level):
    pass


@pytest.mark.board_params("sampling", "ain.sampling")
@scenario("ain.feature", "ain.read converts mid-supply with every sampling time")
def test_read_sampling(sampling):
    pass


@scenario("ain.feature", "The temperature sensor reads room temperature in whole degrees")
def test_temperature():
    pass


@pytest.mark.board_params("n", "ain.burst.n")
@scenario("ain.feature", "A burst of n samples delivers exactly n in n / rate")
def test_burst_count(n):
    pass


@pytest.mark.board_params("n", "ain.burst.n")
@scenario("ain.feature", "A second burst on the same driver again delivers n samples in n / rate")
def test_burst_twice(n):
    pass


@pytest.mark.board_params("pin", "ain.pins")
@scenario("ain.feature", "Every sample of a burst reads the driven level")
def test_burst_values(pin):
    pass


@scenario("ain.feature", "A burst follows a wavegen ramp")
def test_burst_follows_a_ramp():
    pass


@scenario("ain.feature", "One group at a time holds the ADC, and a PWM on TIM2 blocks the burst only")
def test_busy_with_adc_and_tim2():
    pass


@scenario("ain.feature", "Malformed and out-of-range commands are refused")
def test_errors():
    pass


@given("the pin is driven to the level, measured by the scope where it is wired", target_fixture="volts")
def pin_at_level(ad3, need, pin, level):
    return apply_level(ad3, need, pin, level)


@given(parsers.parse("the pin is driven to {programmed:g} V, measured by the scope where it is wired"), target_fixture="volts")
def pin_at(ad3, need, pin, programmed):
    return apply_level(ad3, need, pin, programmed)


@given(parsers.parse("the first pin is driven to {programmed:g} V, measured by the scope where it is wired"), target_fixture="volts")
def first_pin_at(ad3, need, ain_cfg, programmed):
    return apply_level(ad3, need, ain_cfg["pins"][0], programmed)


@given("the first pin is unloaded")
def first_unloaded(need, ain_cfg):
    need.unloaded(ain_cfg["pins"][0])


@given("the first two pins are unloaded")
def first_two_unloaded(need, ain_cfg):
    pin, other = ain_cfg["pins"][:2]
    need.unloaded(pin, other)


@given("the sample count is below the burst buffer")
def below_buffer(ain_cfg, n):
    assert n < ain_cfg["burst"]["buffer"]


@given("the wavegen drives the ramp on the first pin")
def ramp_driven(ad3, need, ain_cfg):
    settings = ain_cfg["ramp"]
    ad3.wavegen.ramp(need.wavegen(ain_cfg["pins"][0]), settings["low"], settings["high"], settings["frequency"])


@given(parsers.parse("the host waits {seconds:g} s"))
def host_waits(seconds):
    time.sleep(seconds)


@when("the temperature sensor is read with the temperature sampling time", target_fixture="reading")
def temperature_read(fw, adc_cfg, ain_cfg):
    return fw.ain.read(adc_cfg["adc"], "temp", sampling=ain_cfg["temp_sampling"])


@when("the first pin is measured in a burst of the sample count at the burst rate, as statistics", target_fixture="burst")
def burst_of_count(fw, adc_cfg, ain_cfg, n):
    (run,) = fw.ain.burst(adc_cfg["adc"], ain_cfg["pins"][0], n=n, rate=ain_cfg["burst"]["rate"], out="stats")
    return run


@when(
    parsers.parse("the first pin is measured in {repeat:d} bursts of the sample count at the burst rate, as statistics"),
    target_fixture="bursts",
)
def bursts_of_count(fw, adc_cfg, ain_cfg, n, repeat):
    return fw.ain.burst(adc_cfg["adc"], ain_cfg["pins"][0], n=n, rate=ain_cfg["burst"]["rate"], repeat=repeat, out="stats")


@when(parsers.parse("the pin is measured in a burst of {count:d} samples at the burst rate"), target_fixture="burst")
def burst_of_pin(fw, adc_cfg, ain_cfg, pin, count):
    (run,) = fw.ain.burst(adc_cfg["adc"], pin, n=count, rate=ain_cfg["burst"]["rate"])
    return run


@when("the first pin is measured in a burst of the sample count of the ramp at the rate of the ramp", target_fixture="burst")
def burst_of_ramp(fw, adc_cfg, ain_cfg):
    settings = ain_cfg["ramp"]
    (run,) = fw.ain.burst(adc_cfg["adc"], ain_cfg["pins"][0], n=settings["n"], rate=settings["rate"])
    return run


@when("the ADC is opened on the second pin")
def adc_opened(fw, adc_cfg, ain_cfg):
    fw.adc.open(adc_cfg["adc"], pins=[ain_cfg["pins"][1]])


@when("the ADC is closed")
def adc_closed(fw, adc_cfg):
    fw.adc.close(adc_cfg["adc"])


@when("PWM opens TIM2 on the pin of its first channel")
def pwm_on_tim2(fw, board_cfg):
    tim2 = next(timer for timer in board_cfg.param("pwm.timers") if timer["timer"] == 2)
    fw.pwm.open(2, pins=[tim2["channels"][0]["pin"]])


@when("PWM closes TIM2")
def pwm_closed(fw):
    fw.pwm.close(2)


@then("one conversion of the pin reads the level as its code within the tolerance")
def pin_reads_level(fw, adc_cfg, pin, volts):
    code = fw.ain.read(adc_cfg["adc"], pin).code
    assert code == pytest.approx(expected_code(volts, adc_cfg), abs=adc_cfg["tolerance_codes"]), f"{volts:.3f} V"


@then("one conversion of the first pin with the sampling time reads the level as its code within the tolerance")
def first_pin_reads_level(fw, adc_cfg, ain_cfg, sampling, volts):
    code = fw.ain.read(adc_cfg["adc"], ain_cfg["pins"][0], sampling=sampling).code
    assert code == pytest.approx(expected_code(volts, adc_cfg), abs=adc_cfg["tolerance_codes"])


@then("the reading is in whole degrees")
def whole_degrees(reading):
    assert reading.mcelsius is not None and reading.mcelsius % 1000 == 0, reading


@then("it is within the temperature range")
def room_temperature(ain_cfg, reading):
    low, high = ain_cfg["temp_range"]
    assert low * 1000 <= reading.mcelsius <= high * 1000, f"{reading.mcelsius / 1000} C"


@then("the temperature sensor read with the default sampling time gives a code above 0 and below full scale")
def default_sampling_code(fw, adc_cfg):
    default = fw.ain.read(adc_cfg["adc"], "temp")
    assert 0 < default.code < 1 << adc_cfg["bits"]


@then("the burst delivered the sample count")
def burst_count(burst, n):
    assert burst.n == n


@then("it took the time of the sample count at the burst rate within the tolerance and the latency")
def burst_timing(board_cfg, ain_cfg, burst, n):
    check_timing(burst, n, board_cfg, ain_cfg)


@then("both bursts delivered the sample count")
def bursts_count(bursts, n):
    assert [run.n for run in bursts] == [n, n]


@then("each took the time of the sample count at the burst rate within the tolerance and the latency")
def bursts_timing(board_cfg, ain_cfg, bursts, n):
    for run in bursts:
        check_timing(run, n, board_cfg, ain_cfg)


@then(parsers.parse("the burst holds {count:d} samples"))
def burst_holds(burst, count):
    assert burst.samples is not None and len(burst.samples) == count


@then("every sample is the level as its code within the tolerance")
def samples_at_level(adc_cfg, burst, volts):
    expected = expected_code(volts, adc_cfg)
    assert all(abs(sample - expected) <= adc_cfg["tolerance_codes"] for sample in burst.samples), burst.samples


@then("the burst holds samples")
def burst_has_samples(burst):
    assert burst.samples is not None


@then(parsers.parse("the samples split at the wraps of the ramp into at most {most:d} rising pieces"), target_fixture="pieces")
def ramp_pieces(adc_cfg, ain_cfg, burst, most):
    span = ramp_span(ain_cfg, adc_cfg)
    pieces: list[list[int]] = [[]]
    for sample in burst.samples:
        if pieces[-1] and sample < pieces[-1][-1] - span / 2:
            pieces.append([])
        pieces[-1].append(sample)
    assert len(pieces) <= most, f"{len(pieces) - 1} wraps in one burst"
    return pieces


@then(parsers.parse("the longest piece rises at the ramp's slope per TIM2 period within {percent:d} %"))
def ramp_slope(board_cfg, adc_cfg, ain_cfg, pieces, percent):
    settings = ain_cfg["ramp"]
    span = ramp_span(ain_cfg, adc_cfg)
    longest = max(pieces, key=len)
    slope = statistics.linear_regression(range(len(longest)), longest).slope
    rate = analog.trigger_rate(board_cfg.clock("timer"), settings["rate"])
    expected = span * settings["frequency"] / RAMP_RISE_FRACTION / rate
    assert slope == pytest.approx(expected, rel=percent / 100), f"{slope:.2f} codes per sample, the ramp gives {expected:.2f}"


@then(parsers.parse('reading the first pin and the temperature sensor fails with "{reason}"'))
def reads_refused(fw, adc_cfg, ain_cfg, reason):
    for target in (ain_cfg["pins"][0], "temp"):
        with pytest.raises(FirmwareError) as error:
            fw.ain.read(adc_cfg["adc"], target)
        assert error.value.reason == reason, target


@then(parsers.parse('a burst of {count:d} samples at {per_second:d} per second on the first pin fails with "{reason}"'))
def burst_refused(fw, adc_cfg, ain_cfg, count, per_second, reason):
    with pytest.raises(FirmwareError) as error:
        fw.ain.burst(adc_cfg["adc"], ain_cfg["pins"][0], n=count, rate=per_second)
    assert error.value.reason == reason, "burst"


@then(
    parsers.parse('a burst of {count:d} samples at {per_second:d} per second on the first pin fails with "{reason}" while PWM holds TIM2')
)
def burst_refused_for_pwm(fw, adc_cfg, ain_cfg, count, per_second, reason):
    with pytest.raises(FirmwareError) as error:
        fw.ain.burst(adc_cfg["adc"], ain_cfg["pins"][0], n=count, rate=per_second)
    assert error.value.reason == reason, "TIM2 held by pwm"


@then("reading the first pin succeeds")
def first_pin_reads(fw, adc_cfg, ain_cfg):
    fw.ain.read(adc_cfg["adc"], ain_cfg["pins"][0])


@then(parsers.parse("a burst of {count:d} samples at {per_second:d} per second on the first pin delivers {delivered:d} samples"))
def burst_delivers(fw, adc_cfg, ain_cfg, count, per_second, delivered):
    assert fw.ain.burst(adc_cfg["adc"], ain_cfg["pins"][0], n=count, rate=per_second)[0].n == delivered


@then("the ADC opens on the second pin, triggered by TIM2")
def adc_opens_on_tim2(fw, adc_cfg, ain_cfg):
    fw.adc.open(adc_cfg["adc"], pins=[ain_cfg["pins"][1]], timer=2)


@then("every malformed or out-of-range ain.read and ain.burst fails with its reason")
def command_errors(fw, board_cfg, adc_cfg, ain_cfg):
    adc, pin = adc_cfg["adc"], ain_cfg["pins"][0]
    digital = board_cfg.param("gpio.output_pins")[0]
    missing = next(number for number in range(8) if number != adc)
    burst = {"n": 16, "rate": 1000}
    cases = [
        (("ain.read", adc), {}, "usage"),
        (("ain.read", adc, pin), {"sampling": "1"}, "usage"),
        (("ain.read", missing, pin), {}, "range"),
        (("ain.read", adc, digital), {}, "pin"),
        (("ain.read", adc, "nosuchpin"), {}, "pin"),
        (("ain.burst", adc, pin), {"n": 16}, "usage"),
        (("ain.burst", adc, pin), {"rate": 1000}, "usage"),
        (("ain.burst", adc, pin), {**burst, "n": 0}, "range"),
        (("ain.burst", adc, pin), {**burst, "n": analog.AIN_BUFFER + 1}, "range"),
        (("ain.burst", adc, pin), {**burst, "n": analog.AIN_LIST_MAX + 1}, "range"),
        (("ain.burst", adc, pin), {**burst, "rate": analog.AIN_RATE_MIN - 1}, "range"),
        (("ain.burst", adc, pin), {**burst, "rate": analog.AIN_RATE_MAX + 1}, "range"),
        (("ain.burst", adc, pin), {**burst, "repeat": analog.AIN_REPEAT_MAX + 1}, "range"),
        (("ain.burst", adc, pin), {**burst, "out": "hex"}, "usage"),
        (("ain.burst", missing, pin), burst, "range"),
        (("ain.burst", adc, digital), burst, "pin"),
    ]
    for (name, *args), options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.command(name, *args, **options)
        assert error.value.reason == reason, (name, args, options)
