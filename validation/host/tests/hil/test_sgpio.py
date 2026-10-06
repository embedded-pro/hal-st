"""Synchronous GPIO (`hal::SynchronousOutputPinStm`, `hal::SmallPeripheralPinStm`, `hal::MultiGpioPinStm` with
`hal::MultiPeripheralPinStm`) through the `sgpio` group.

Wiring set `bundle1`: the pins of `tests.sgpio` on DIOs. The output pins are read with the AD3 static I/O. An open-drain
output has no pull (`SynchronousOutputPinStm` takes none): on `out_pins` only its low level and its latch are asserted,
on `float_pins` (no board load) the AD3 pulls show that a high level releases the line. The alternate-function cases
mux a timer channel to the pins while the `tpwm` group runs that channel with no pin of its own (`-`): the PWM appears
on every muxed pin exactly when the mux works.

Scenarios: features/sgpio.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.groups.timers import pwm_duty_fraction, timer_update_rate


@pytest.fixture
def sgpio_cfg(board_cfg):
    cfg = board_cfg.param("sgpio", None)
    if cfg is None:
        pytest.skip("tests.sgpio not configured")
    return cfg


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def follows_pulls(ad3, dio):
    """An input without pull reads whatever pull the AD3 applies."""
    ad3.dio.pull(up=[dio])
    up = ad3.dio.read(dio)
    ad3.dio.pull(down=[dio])
    down = ad3.dio.read(dio)
    ad3.dio.pull()
    return up == 1 and down == 0


def run_channel(fw, cfg):
    """Start the timer of `tests.sgpio.tpwm` with its first channel unused (`-`); returns the PWM frequency and the
    expected high fraction."""
    if not hasattr(fw, "tpwm"):
        pytest.skip("needs the tpwm group")
    tpwm = cfg["tpwm"]
    pins = [None if pin == "-" else pin for pin in tpwm["pins"]]
    timclk = fw.tpwm.open(tpwm["timer"], pins, prescaler=tpwm["prescaler"], period=tpwm["period"])
    fw.tpwm.duty(tpwm["timer"], 1, tpwm["duty"])
    fw.tpwm.start(tpwm["timer"])
    return timer_update_rate(timclk, tpwm["prescaler"], tpwm["period"]), pwm_duty_fraction(tpwm["period"], tpwm["duty"])


def check_pwm(ad3, cfg, dios, frequency, fraction):
    periods = cfg["record_periods"]
    capture = ad3.logic.record_for(periods / frequency, timeout=periods / frequency + 2)
    tolerance = cfg["tolerance"]
    for dio in dios:
        assert capture.frequency(dio) == pytest.approx(frequency, rel=tolerance["frequency"]), f"DIO{dio} frequency"
        quantisation = 2 * frequency / capture.rate
        assert capture.duty(dio) == pytest.approx(fraction, abs=tolerance["duty"] + quantisation), f"DIO{dio} duty"


@pytest.mark.board_params("pin", "sgpio.out_pins")
@pytest.mark.board_params("speed", "sgpio.speeds")
@scenario("sgpio.feature", "A push-pull output drives the DIO with every speed and latches the level")
def test_push_pull_levels(pin, speed):
    pass


@pytest.mark.board_params("pin", "sgpio.out_pins")
@scenario("sgpio.feature", "An open-drain output pulls low and keeps open drain on a later out")
def test_open_drain_low_and_latch(pin):
    pass


@pytest.mark.board_params("pin", "sgpio.float_pins")
@scenario("sgpio.feature", "Open drain releases the line, push-pull drives it and the released pin floats")
def test_switch_drive_and_release(pin):
    pass


@pytest.mark.usefixtures("sgpio_cfg")
@scenario("sgpio.feature", "A timer channel muxed to the alternate-function pin drives the PWM there")
def test_af_by_timer():
    pass


@pytest.mark.usefixtures("sgpio_cfg")
@scenario("sgpio.feature", "A timer channel muxed to the set drives the PWM on every pin of it")
def test_multi():
    pass


@pytest.mark.usefixtures("sgpio_cfg")
@scenario("sgpio.feature", "A raw alternate function number is muxed and reported")
def test_af_number():
    pass


@pytest.mark.usefixtures("sgpio_cfg")
@scenario("sgpio.feature", "The group has four outputs, four alternate-function pins and one set")
def test_slots():
    pass


@pytest.mark.usefixtures("sgpio_cfg")
@scenario("sgpio.feature", "A pin serves either the gpio or the sgpio group")
def test_shared_with_gpio():
    pass


@pytest.mark.usefixtures("sgpio_cfg")
@scenario("sgpio.feature", "Malformed, out-of-range, reserved and foreign commands are refused")
def test_errors():
    pass


@given("the pin is wired to a DIO", target_fixture="dio")
def pin_wired(need, pin):
    return need.dio(pin)


@given("the alternate-function pin is wired to a DIO", target_fixture="dio")
def af_pin_wired(need, sgpio_cfg):
    return need.dio(sgpio_cfg["af"]["pin"])


@given("the pins of the set are wired to DIOs", target_fixture="dios")
def set_wired(need, sgpio_cfg):
    return [need.dio(pin) for pin in sgpio_cfg["multi"]["pins"]]


@given("the DIO is released")
def dio_released(ad3, dio):
    ad3.dio.release(dio)


@given("the timer of tpwm runs its first channel with no pin", target_fixture="pwm_signal")
def channel_running(fw, sgpio_cfg):
    return run_channel(fw, sgpio_cfg)


@given("the first output pin is configured as a GPIO input")
def gpio_input(fw, sgpio_cfg):
    fw.gpio.cfg(sgpio_cfg["out_pins"][0], "in")


@when(parsers.parse("the pin is set as an open-drain output to {level:d}"))
def open_drain_out(fw, pin, level):
    fw.sgpio.out(pin, level, od=True)


@when(parsers.parse("the pin is set as a push-pull output to {level:d}"))
def push_pull_out(fw, pin, level):
    fw.sgpio.out(pin, level, od=False)


@when(parsers.parse("the pin is set to {level:d}"))
def out(fw, pin, level):
    fw.sgpio.out(pin, level)


@when("the pin is released")
def pin_released(fw, pin):
    fw.sgpio.release(pin)


@when("the alternate-function pin is released")
@then("the alternate-function pin is released")
def af_pin_released(fw, sgpio_cfg):
    fw.sgpio.release(sgpio_cfg["af"]["pin"])


@when("the pins of the set are muxed to the timer channel")
def set_muxed(fw, sgpio_cfg):
    multi = sgpio_cfg["multi"]
    fw.sgpio.multi(multi["pins"], timer=multi["timer"], ch=multi["ch"])


@when("the last pin of the set is released")
def last_of_set_released(fw, sgpio_cfg):
    fw.sgpio.release(sgpio_cfg["multi"]["pins"][-1])


@when("the first pin of the set is released")
def first_of_set_released(fw, sgpio_cfg):
    fw.sgpio.release(sgpio_cfg["multi"]["pins"][0])


@when("the AD3 pulls the DIOs of the set down")
def set_pulled_down(ad3, dios):
    ad3.dio.pull(down=dios)


@when("the first four limit pins are set as outputs to 0")
def limit_outputs(fw, sgpio_cfg):
    pins = sgpio_cfg["limit_pins"]
    for pin in pins[:4]:
        fw.sgpio.out(pin, 0)


@when("the first four limit pins are released")
def limit_released(fw, sgpio_cfg):
    pins = sgpio_cfg["limit_pins"]
    for pin in pins[:4]:
        fw.sgpio.release(pin)


@when("alternate function 0 is muxed to the first four limit pins")
def limit_alternate_functions(fw, sgpio_cfg):
    pins = sgpio_cfg["limit_pins"]
    for pin in pins[:4]:
        fw.sgpio.af(pin, af=0)


@when("the GPIO of the first output pin is released")
def gpio_released(fw, sgpio_cfg):
    fw.gpio.release(sgpio_cfg["out_pins"][0])


@when("the first output pin is set as an sgpio output to 0")
def sgpio_output(fw, sgpio_cfg):
    fw.sgpio.out(sgpio_cfg["out_pins"][0], 0)


@then("the pin, set as an output with the speed to every level in turn, drives the DIO to it and latches it")
def push_pull_levels(fw, ad3, pin, dio, speed):
    for level in (1, 0, 1, 0):
        fw.sgpio.out(pin, level, speed=speed)
        assert ad3.dio.read(dio) == level, f"{pin} set to {level}"
        assert fw.sgpio.latch(pin) == level


@then("the open drain pulls the DIO low")
def open_drain_pulls_low(ad3, dio):
    assert ad3.dio.read(dio) == 0, "open drain must pull low"


@then(parsers.parse("the latch of the pin reads {level:d}"))
def latch_reads(fw, pin, level):
    assert fw.sgpio.latch(pin) == level


@then("the latch of the pin reads 1: a later out keeps open drain and sets the latch")
def latch_kept(fw, pin):
    assert fw.sgpio.latch(pin) == 1, "a later out keeps open drain and sets the latch"


@then(parsers.parse("the DIO reads {level:d}"))
def dio_reads(ad3, dio, level):
    assert ad3.dio.read(dio) == level


@then("the DIO reads high after open drain")
def dio_high_after_open_drain(ad3, dio):
    assert ad3.dio.read(dio) == 1, "push-pull high after open drain"


@then("the line follows the AD3 pulls: open drain at 1 releases it")
def open_drain_releases(ad3, pin, dio):
    assert follows_pulls(ad3, dio), f"{pin} open drain at 1 must release the line"


@then("the line does not follow the AD3 pulls: open drain at 0 holds it low")
def open_drain_holds_low(ad3, pin, dio):
    assert not follows_pulls(ad3, dio), f"{pin} open drain at 0 must hold the line low"


@then("the line does not follow the AD3 pulls: push-pull high overrides the AD3 pull-down")
def push_pull_overrides(ad3, pin, dio):
    assert not follows_pulls(ad3, dio), f"{pin} push-pull high must override the AD3 pull-down"


@then("the line follows the AD3 pulls: the released pin is an input without pull")
def released_floats(ad3, pin, dio):
    assert follows_pulls(ad3, dio), f"{pin} must be an input without pull after sgpio.release"


@then("muxing the alternate-function pin to the timer channel reports its alternate function number")
def af_by_timer(fw, sgpio_cfg):
    af = sgpio_cfg["af"]
    assert fw.sgpio.af(af["pin"], timer=af["timer"], ch=af["ch"]) == af["af"]


@then("the PWM appears on the DIO")
def pwm_on_dio(ad3, sgpio_cfg, dio, pwm_signal):
    frequency, fraction = pwm_signal
    check_pwm(ad3, sgpio_cfg, [dio], frequency, fraction)


@then("the line follows the AD3 pulls: SmallPeripheralPinStm leaves an input without pull")
def af_released_floats(ad3, dio):
    assert follows_pulls(ad3, dio), "SmallPeripheralPinStm must leave an input without pull"


@then("the PWM appears on every DIO of the set")
def pwm_on_set(ad3, sgpio_cfg, dios, pwm_signal):
    frequency, fraction = pwm_signal
    check_pwm(ad3, sgpio_cfg, dios, frequency, fraction)


@then("no DIO of the set shows an edge for the recording periods")
def set_unmuxed(ad3, sgpio_cfg, dios, pwm_signal):
    frequency, _ = pwm_signal
    capture = ad3.logic.record_for(sgpio_cfg["record_periods"] / frequency, timeout=sgpio_cfg["record_periods"] / frequency + 2)
    for dio in dios:
        assert analysis.edge_count(capture.channel(dio)) == 0, f"DIO{dio} still muxed after releasing the set"


@then("muxing the alternate-function pin to its raw alternate function number reports that number")
def af_number(fw, sgpio_cfg):
    pin = sgpio_cfg["af"]["pin"]
    assert fw.sgpio.af(pin, af=sgpio_cfg["af"]["af"]) == sgpio_cfg["af"]["af"]


@then(parsers.parse('setting the fifth limit pin as an output to 0 fails with "{reason}"'))
def fifth_output_refused(fw, sgpio_cfg, reason):
    expect_error(reason, fw.sgpio.out, sgpio_cfg["limit_pins"][4], 0)


@then(parsers.parse('muxing alternate function 0 to the first limit pin fails with "{reason}"'))
def first_af_refused(fw, sgpio_cfg, reason):
    expect_error(reason, fw.sgpio.af, sgpio_cfg["limit_pins"][0], af=0)


@then(parsers.parse('muxing alternate function 0 to the fifth limit pin fails with "{reason}"'))
def fifth_af_refused(fw, sgpio_cfg, reason):
    expect_error(reason, fw.sgpio.af, sgpio_cfg["limit_pins"][4], af=0)


@then(parsers.parse('setting the first limit pin as an output to 0 fails with "{reason}"'))
def first_output_refused(fw, sgpio_cfg, reason):
    expect_error(reason, fw.sgpio.out, sgpio_cfg["limit_pins"][0], 0)


@then(parsers.parse('muxing the first pin of the set alone to the timer channel fails with "{reason}"'))
def second_set_refused(fw, sgpio_cfg, reason):
    multi = sgpio_cfg["multi"]
    expect_error(reason, fw.sgpio.multi, multi["pins"][:1], timer=multi["timer"], ch=multi["ch"])


@then(parsers.parse('setting the first pin of the set as an output to 0 fails with "{reason}"'))
def set_pin_output_refused(fw, sgpio_cfg, reason):
    expect_error(reason, fw.sgpio.out, sgpio_cfg["multi"]["pins"][0], 0)


@then("every pin of the set can be set as an output to 0")
def set_pins_outputs(fw, sgpio_cfg):
    for pin in sgpio_cfg["multi"]["pins"]:
        fw.sgpio.out(pin, 0)


@then(parsers.parse('setting the first output pin as an sgpio output to 1 fails with "{reason}"'))
def sgpio_refused(fw, sgpio_cfg, reason):
    expect_error(reason, fw.sgpio.out, sgpio_cfg["out_pins"][0], 1)


@then(parsers.parse('configuring the first output pin as a GPIO input fails with "{reason}"'))
def gpio_refused(fw, sgpio_cfg, reason):
    expect_error(reason, fw.gpio.cfg, sgpio_cfg["out_pins"][0], "in")


@then("every malformed, out-of-range, reserved or foreign sgpio command line fails with its reason")
def errors(fw, board_cfg, sgpio_cfg):
    out_pin = sgpio_cfg["out_pins"][0]
    af = sgpio_cfg["af"]
    multi = sgpio_cfg["multi"]
    reserved = board_cfg.terminal.pins[0]
    unbonded = board_cfg.param("system.unbonded_pins")[0]
    for line, reason in (
        (f"sgpio.out {fw.pin(out_pin)} 2", "range"),
        (f"sgpio.out {fw.pin(out_pin)} 1 od=2", "range"),
        (f"sgpio.out {fw.pin(out_pin)} 1 speed=fastest", "usage"),
        (f"sgpio.out {fw.pin(out_pin)}", "usage"),
        (f"sgpio.out {fw.pin(out_pin)} 1 pull=up", "usage"),
        (f"sgpio.out {reserved} 1", "busy"),
        (f"sgpio.out {unbonded} 1", "pin"),
        ("sgpio.out nosuchpin 1", "pin"),
        (f"sgpio.latch {fw.pin(out_pin)}", "notopen"),
        (f"sgpio.release {fw.pin(out_pin)}", "notopen"),
        (f"sgpio.af {fw.pin(af['pin'])}", "usage"),
        (f"sgpio.af {fw.pin(af['pin'])} timer={af['timer']} af=1", "usage"),
        (f"sgpio.af {fw.pin(af['pin'])} af=1 ch=1", "usage"),
        (f"sgpio.af {fw.pin(af['pin'])} timer=0", "range"),
        (f"sgpio.af {fw.pin(af['pin'])} timer=18", "range"),
        (f"sgpio.af {fw.pin(af['pin'])} timer={sgpio_cfg['missing_timer']}", "range"),
        (f"sgpio.af {fw.pin(af['pin'])} timer={af['timer']} ch=5", "range"),
        (f"sgpio.af {fw.pin(af['pin'])} af=16", "range"),
        (f"sgpio.af {fw.pin(sgpio_cfg['foreign_pin'])} timer={af['timer']} ch={af['ch']}", "pin"),
        (f"sgpio.af {unbonded} af=1", "pin"),
        (f"sgpio.af {reserved} af=1", "busy"),
        (f"sgpio.multi {fw.pin(multi['pins'][0])}", "usage"),
        (f"sgpio.multi , timer={multi['timer']}", "usage"),
        (f"sgpio.multi {fw.pin(multi['pins'][0])},{fw.pin(multi['pins'][0])} timer={multi['timer']}", "usage"),
        (f"sgpio.multi {','.join([fw.pin(multi['pins'][0])] * 5)} timer={multi['timer']}", "usage"),
        (f"sgpio.multi {fw.pin(multi['pins'][0])} timer={sgpio_cfg['missing_timer']}", "range"),
        (f"sgpio.multi {fw.pin(sgpio_cfg['foreign_pin'])} timer={multi['timer']}", "pin"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == reason, line
