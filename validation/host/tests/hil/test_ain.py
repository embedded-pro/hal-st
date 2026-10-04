"""Analog input (`hal::AnalogToDigitalPinImplStm`, `hal::AnalogToDigitalInternalTemperatureStm`,
`hal::AdcTriggeredByTimerWithDma`, PROTOCOL.md D.11): single conversions against wavegen levels, the temperature
sensor, and TIM2-paced bursts on one driver over a 256-sample buffer (B.12: `Measure(n)` delivers exactly n samples
in n / rate, also a second time on the same driver).

Wiring set `bundle1`: W1/W2 and the scope on `tests.ain.pins` (the ADC inputs of `tests.adc`); the burst counts,
the temperature and the argument checks need no AD3.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import expect
from hal_st_validation.groups import analog


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


@pytest.mark.ad3
@pytest.mark.board_params("pin", "ain.pins")
@pytest.mark.board_params("level", "ain.levels")
def test_read_dc(fw, ad3, need, adc_cfg, pin, level):
    """`ain.read` converts the pin once (`AnalogToDigitalPinImplStm`)."""
    volts = apply_level(ad3, need, pin, level)
    code = fw.ain.read(adc_cfg["adc"], pin).code
    assert code == pytest.approx(expected_code(volts, adc_cfg), abs=adc_cfg["tolerance_codes"]), f"{volts:.3f} V"


@pytest.mark.ad3
@pytest.mark.board_params("sampling", "ain.sampling")
def test_read_sampling(fw, ad3, need, adc_cfg, ain_cfg, sampling):
    pin = ain_cfg["pins"][0]
    volts = apply_level(ad3, need, pin, 1.65)
    code = fw.ain.read(adc_cfg["adc"], pin, sampling=sampling).code
    assert code == pytest.approx(expected_code(volts, adc_cfg), abs=adc_cfg["tolerance_codes"])


def test_temperature(fw, adc_cfg, ain_cfg):
    """The internal sensor through `__LL_ADC_CALC_TEMPERATURE` (whole degrees) is at room temperature with a long
    sampling time; with the driver's default, below the sensor's minimum (B.14), a code still arrives."""
    low, high = ain_cfg["temp_range"]
    reading = fw.ain.read(adc_cfg["adc"], "temp", sampling=ain_cfg["temp_sampling"])
    assert reading.mcelsius is not None and reading.mcelsius % 1000 == 0, reading
    assert low * 1000 <= reading.mcelsius <= high * 1000, f"{reading.mcelsius / 1000} C"
    default = fw.ain.read(adc_cfg["adc"], "temp")
    assert 0 < default.code < 1 << adc_cfg["bits"]


@pytest.mark.board_params("n", "ain.burst.n")
def test_burst_count(fw, need, board_cfg, adc_cfg, ain_cfg, n):
    """`Measure(n)` with n below the buffer delivers exactly n samples in n / rate; the unfixed driver fills the
    whole buffer (256 samples in 256 / rate)."""
    pin = ain_cfg["pins"][0]
    need.unloaded(pin)
    assert n < ain_cfg["burst"]["buffer"]
    (run,) = fw.ain.burst(adc_cfg["adc"], pin, n=n, rate=ain_cfg["burst"]["rate"], out="stats")
    assert run.n == n
    check_timing(run, n, board_cfg, ain_cfg)


@pytest.mark.board_params("n", "ain.burst.n")
def test_burst_twice(fw, need, board_cfg, adc_cfg, ain_cfg, n):
    """`repeat=2` measures twice on one driver: the second `Measure(n)` starts from the state the first left (the
    unfixed driver leaves the ADC enabled with conversions pending) and again gives n samples in n / rate."""
    pin = ain_cfg["pins"][0]
    need.unloaded(pin)
    runs = fw.ain.burst(adc_cfg["adc"], pin, n=n, rate=ain_cfg["burst"]["rate"], repeat=2, out="stats")
    assert [run.n for run in runs] == [n, n]
    for run in runs:
        check_timing(run, n, board_cfg, ain_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("pin", "ain.pins")
def test_burst_values(fw, ad3, need, adc_cfg, ain_cfg, pin):
    volts = apply_level(ad3, need, pin, 2.0)
    (run,) = fw.ain.burst(adc_cfg["adc"], pin, n=16, rate=ain_cfg["burst"]["rate"])
    assert run.samples is not None and len(run.samples) == 16
    expected = expected_code(volts, adc_cfg)
    assert all(abs(sample - expected) <= adc_cfg["tolerance_codes"] for sample in run.samples), run.samples


@pytest.mark.ad3
def test_burst_follows_a_ramp(fw, ad3, need, board_cfg, adc_cfg, ain_cfg):
    """A wavegen ramp during the burst: the samples rise at the ramp's slope per TIM2 period (the window holds at
    most one wrap of the sawtooth, so the samples split into at most two rising pieces)."""
    settings = ain_cfg["ramp"]
    pin = ain_cfg["pins"][0]
    ad3.wavegen.ramp(need.wavegen(pin), settings["low"], settings["high"], settings["frequency"])
    time.sleep(0.05)
    (run,) = fw.ain.burst(adc_cfg["adc"], pin, n=settings["n"], rate=settings["rate"])
    assert run.samples is not None
    span = expected_code(settings["high"], adc_cfg) - expected_code(settings["low"], adc_cfg)
    pieces: list[list[int]] = [[]]
    for sample in run.samples:
        if pieces[-1] and sample < pieces[-1][-1] - span / 2:
            pieces.append([])
        pieces[-1].append(sample)
    assert len(pieces) <= 2, f"{len(pieces) - 1} wraps in one burst"
    longest = max(pieces, key=len)
    slope = statistics.linear_regression(range(len(longest)), longest).slope
    rate = analog.trigger_rate(board_cfg.clock("timer"), settings["rate"])
    expected = span * settings["frequency"] / rate
    assert slope == pytest.approx(expected, rel=0.3), f"{slope:.2f} codes per sample, the ramp gives {expected:.2f}"


def test_busy_with_adc_and_tim2(fw, need, board_cfg, adc_cfg, ain_cfg):
    """One group at a time holds the ADC: while `adc` is open `ain.read` and `ain.burst` answer `ERR busy`. The
    burst also needs TIM2 and the ADC's DMA channel, so a PWM on TIM2 blocks it while `ain.read` works."""
    adc = adc_cfg["adc"]
    pin, other = ain_cfg["pins"][:2]
    need.unloaded(pin, other)
    fw.adc.open(adc, pins=[other])
    for target in (pin, "temp"):
        with pytest.raises(FirmwareError) as error:
            fw.ain.read(adc, target)
        assert error.value.reason == "busy", target
    with pytest.raises(FirmwareError) as error:
        fw.ain.burst(adc, pin, n=16, rate=1000)
    assert error.value.reason == "busy", "burst"
    fw.adc.close(adc)
    tim2 = next(timer for timer in board_cfg.param("pwm.timers") if timer["timer"] == 2)
    fw.pwm.open(2, pins=[tim2["channels"][0]["pin"]])
    with pytest.raises(FirmwareError) as error:
        fw.ain.burst(adc, pin, n=16, rate=1000)
    assert error.value.reason == "busy", "TIM2 held by pwm"
    fw.ain.read(adc, pin)
    fw.pwm.close(2)
    assert fw.ain.burst(adc, pin, n=16, rate=1000)[0].n == 16
    fw.adc.open(adc, pins=[other], timer=2)


def test_errors(fw, board_cfg, adc_cfg, ain_cfg):
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
