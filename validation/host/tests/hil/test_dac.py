"""DAC outputs (`hal::DigitalToAnalogPinImplStm`) through the `dac` group: the output voltage against the code, the rails
of the buffer, both outputs at once, the clamping of the code, argument errors and the pin ownership.

Wiring set `bundle3` with `--with dac`: scope 1 on DAC1 OUT1 (PA4) and scope 2 on DAC2 OUT1 (PA6), W1/W2 unplugged. The
cases that read the voltage skip without it; the others need no AD3.

Scenarios: features/dac.feature.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.groups.dac import DAC_FULL_SCALE, dac_volts

SCOPE_SAMPLES = 2000
SCOPE_RATE = 2e4


@pytest.fixture
def dac_cfg(board_cfg):
    return board_cfg.param("dac")


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def measure(ad3, channel: int, cfg) -> float:
    """Mean voltage on a scope channel over 100 ms after the output settled."""
    time.sleep(cfg["settle_s"])
    return statistics.fmean(ad3.scope.acquire([channel], rate=SCOPE_RATE, samples=SCOPE_SAMPLES, range_v=5.0, offset_v=1.65)[channel])


@pytest.mark.board_params("pin", "dac.pins")
@pytest.mark.board_params("code", "dac.codes")
@scenario("dac.feature", "The output follows the code")
def test_follows_code(pin, code):
    pass


@pytest.mark.board_params("pin", "dac.pins")
@scenario("dac.feature", "The full range reaches the rails of the buffer")
def test_rails(pin):
    pass


@scenario("dac.feature", "Both outputs run at once at their own codes")
def test_both_outputs():
    pass


@pytest.mark.board_params("pin", "dac.pins")
@scenario("dac.feature", "A code above the resolution is clamped")
def test_clamped(pin):
    pass


@pytest.mark.board_params("pin", "dac.pins")
@scenario("dac.feature", "Invalid arguments are refused with their reason")
def test_errors(pin):
    pass


@pytest.mark.board_params("pin", "dac.pins")
@scenario("dac.feature", "The pin returns to the pool on close")
def test_pin_is_released(pin):
    pass


@given("the scope is wired to the DAC output", target_fixture="channel")
def scope_wired(need, pin):
    return need.scope(pin)


@given("the scope is wired to both DAC outputs", target_fixture="channels")
def scopes_wired(need, dac_cfg):
    return [need.scope(pin) for pin in dac_cfg["pins"][:2]]


@when("the output is opened and written with the code")
def open_and_write(fw, pin, code):
    fw.dac.open(pin)
    assert fw.dac.set(pin, code) == code


@when("the output is opened and written with the lowest code")
def open_and_write_low(fw, dac_cfg, pin):
    fw.dac.open(pin)
    fw.dac.set(pin, dac_cfg["low_code"])


@when("the output is written with the highest code")
def write_high(fw, dac_cfg, pin):
    fw.dac.set(pin, dac_cfg["high_code"])


@when("both outputs are opened and written with different codes", target_fixture="codes")
def open_both(fw, dac_cfg):
    first, second = dac_cfg["pins"][:2]
    codes = dac_cfg["codes"][1], dac_cfg["codes"][-2]
    for pin, code in zip((first, second), codes, strict=True):
        fw.dac.open(pin)
        fw.dac.set(pin, code)
    return codes


@when(parsers.parse("the output is opened and written with a code of {code:d}"), target_fixture="reported")
def open_and_write_clamped(fw, pin, code):
    fw.dac.open(pin)
    return fw.dac.set(pin, code)


@when("the output is opened and closed")
def open_and_close(fw, pin):
    fw.dac.open(pin)
    fw.dac.close(pin)


@then("the scope measures the voltage of the code within the tolerance")
def voltage_of_code(ad3, dac_cfg, channel, code):
    volts = measure(ad3, channel, dac_cfg)
    assert volts == pytest.approx(dac_volts(code, dac_cfg["vref"]), abs=dac_cfg["tolerance_v"])


@then("the scope measures at most the low limit")
def low_limit(ad3, dac_cfg, channel):
    volts = measure(ad3, channel, dac_cfg)
    assert volts <= dac_cfg["low_max_v"], volts


@then("the scope measures at least the high limit")
def high_limit(ad3, dac_cfg, channel):
    volts = measure(ad3, channel, dac_cfg)
    assert volts >= dac_cfg["high_min_v"], volts


@then("each scope measures the voltage of its own code within the tolerance")
def each_voltage(ad3, dac_cfg, channels, codes):
    for channel, code in zip(channels, codes, strict=True):
        assert measure(ad3, channel, dac_cfg) == pytest.approx(dac_volts(code, dac_cfg["vref"]), abs=dac_cfg["tolerance_v"])


@then("the firmware reports the highest 12-bit code")
def reports_clamped(reported):
    assert reported == DAC_FULL_SCALE


@then(parsers.parse('opening a pin without a DAC output fails with "{reason}"'))
def foreign_pin_refused(fw, dac_cfg, reason):
    expect_error(reason, fw.dac.open, dac_cfg["foreign_pin"])


@then(parsers.parse('writing a closed output fails with "{reason}"'))
def write_closed_refused(fw, pin, reason):
    expect_error(reason, fw.dac.set, pin, 0)


@then(parsers.parse('closing a closed output fails with "{reason}"'))
def close_closed_refused(fw, pin, reason):
    expect_error(reason, fw.dac.close, pin)


@then(parsers.parse('opening an output twice fails with "{reason}"'))
def open_twice_refused(fw, pin, reason):
    fw.dac.open(pin)
    expect_error(reason, fw.dac.open, pin)


@then(parsers.parse('writing a code beyond 16 bits fails with "{reason}"'))
def write_beyond_refused(fw, pin, reason):
    expect_error(reason, fw.dac.set, pin, 65536)


@then(parsers.parse('a command with a missing argument fails with "{reason}"'))
def missing_argument_refused(fw, reason):
    for line in ("dac.open", "dac.set", "dac.set PA4", "dac.close"):
        assert fw.terminal.command(line, check=False).reason == reason, line


@then("the pin can be configured as a GPIO input")
def pin_is_free(fw, pin):
    fw.gpio.cfg(pin, "in")
    fw.gpio.release(pin)


@then("the output can be opened again")
def reopens(fw, pin):
    fw.dac.open(pin)
