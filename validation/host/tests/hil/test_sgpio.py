"""Synchronous GPIO (`hal::SynchronousOutputPinStm`, `hal::SmallPeripheralPinStm`, `hal::MultiGpioPinStm` with
`hal::MultiPeripheralPinStm`) through the `sgpio` group.

Wiring set `bundle1`: the pins of `tests.sgpio` on DIOs. The output pins are read with the AD3 static I/O. An open-drain
output has no pull (`SynchronousOutputPinStm` takes none): on `out_pins` only its low level and its latch are asserted,
on `float_pins` (no board load) the AD3 pulls show that a high level releases the line. The alternate-function cases
mux a timer channel to the pins while the `tpwm` group runs that channel with no pin of its own (`-`): the PWM appears
on every muxed pin exactly when the mux works.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

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


@pytest.mark.ad3
@pytest.mark.board_params("pin", "sgpio.out_pins")
@pytest.mark.board_params("speed", "sgpio.speeds")
def test_push_pull_levels(fw, ad3, need, pin, speed):
    dio = need.dio(pin)
    ad3.dio.release(dio)
    for level in (1, 0, 1, 0):
        fw.sgpio.out(pin, level, speed=speed)
        assert ad3.dio.read(dio) == level, f"{pin} set to {level}"
        assert fw.sgpio.latch(pin) == level


@pytest.mark.ad3
@pytest.mark.board_params("pin", "sgpio.out_pins")
def test_open_drain_low_and_latch(fw, ad3, need, pin):
    dio = need.dio(pin)
    ad3.dio.release(dio)
    fw.sgpio.out(pin, 0, od=True)
    assert ad3.dio.read(dio) == 0, "open drain must pull low"
    assert fw.sgpio.latch(pin) == 0
    fw.sgpio.out(pin, 1)
    assert fw.sgpio.latch(pin) == 1, "a later out keeps open drain and sets the latch"
    fw.sgpio.out(pin, 0)
    assert ad3.dio.read(dio) == 0


@pytest.mark.ad3
@pytest.mark.board_params("pin", "sgpio.float_pins")
def test_switch_drive_and_release(fw, ad3, need, pin):
    """Open drain releases the line at 1 and holds it low at 0; a changed `od` rebuilds the pin as push-pull, which
    drives the high level against both pulls; `sgpio.release` leaves an input without pull (`float_pins` carry no board load, so the AD3
    pulls decide its level)."""
    dio = need.dio(pin)
    ad3.dio.release(dio)
    fw.sgpio.out(pin, 1, od=True)
    assert follows_pulls(ad3, dio), f"{pin} open drain at 1 must release the line"
    fw.sgpio.out(pin, 0, od=True)
    assert not follows_pulls(ad3, dio), f"{pin} open drain at 0 must hold the line low"
    fw.sgpio.out(pin, 1, od=False)
    assert not follows_pulls(ad3, dio), f"{pin} push-pull high must override the AD3 pull-down"
    assert ad3.dio.read(dio) == 1, "push-pull high after open drain"
    fw.sgpio.release(pin)
    assert follows_pulls(ad3, dio), f"{pin} must be an input without pull after sgpio.release"


@pytest.mark.ad3
def test_af_by_timer(fw, ad3, need, sgpio_cfg):
    af = sgpio_cfg["af"]
    dio = need.dio(af["pin"])
    frequency, fraction = run_channel(fw, sgpio_cfg)
    assert fw.sgpio.af(af["pin"], timer=af["timer"], ch=af["ch"]) == af["af"]
    check_pwm(ad3, sgpio_cfg, [dio], frequency, fraction)
    fw.sgpio.release(af["pin"])
    assert follows_pulls(ad3, dio), "SmallPeripheralPinStm must leave an input without pull"


@pytest.mark.ad3
def test_multi(fw, ad3, need, sgpio_cfg):
    multi = sgpio_cfg["multi"]
    dios = [need.dio(pin) for pin in multi["pins"]]
    frequency, fraction = run_channel(fw, sgpio_cfg)
    fw.sgpio.multi(multi["pins"], timer=multi["timer"], ch=multi["ch"])
    check_pwm(ad3, sgpio_cfg, dios, frequency, fraction)
    fw.sgpio.release(multi["pins"][-1])
    ad3.dio.pull(down=dios)
    capture = ad3.logic.record_for(sgpio_cfg["record_periods"] / frequency, timeout=sgpio_cfg["record_periods"] / frequency + 2)
    for dio in dios:
        assert analysis.edge_count(capture.channel(dio)) == 0, f"DIO{dio} still muxed after releasing the set"


def test_af_number(fw, sgpio_cfg):
    """`af=<n>` muxes a raw alternate function and reports it."""
    pin = sgpio_cfg["af"]["pin"]
    assert fw.sgpio.af(pin, af=sgpio_cfg["af"]["af"]) == sgpio_cfg["af"]["af"]
    fw.sgpio.release(pin)


def test_slots(fw, sgpio_cfg):
    """Four outputs, four alternate-function pins and one set at a time; a pin serves one use."""
    pins = sgpio_cfg["limit_pins"]
    for pin in pins[:4]:
        fw.sgpio.out(pin, 0)
    expect_error("busy", fw.sgpio.out, pins[4], 0)
    expect_error("busy", fw.sgpio.af, pins[0], af=0)
    for pin in pins[:4]:
        fw.sgpio.release(pin)
    for pin in pins[:4]:
        fw.sgpio.af(pin, af=0)
    expect_error("busy", fw.sgpio.af, pins[4], af=0)
    expect_error("busy", fw.sgpio.out, pins[0], 0)
    for pin in pins[:4]:
        fw.sgpio.release(pin)
    multi = sgpio_cfg["multi"]
    fw.sgpio.multi(multi["pins"], timer=multi["timer"], ch=multi["ch"])
    expect_error("busy", fw.sgpio.multi, multi["pins"][:1], timer=multi["timer"], ch=multi["ch"])
    expect_error("busy", fw.sgpio.out, multi["pins"][0], 0)
    fw.sgpio.release(multi["pins"][0])
    for pin in multi["pins"]:
        fw.sgpio.out(pin, 0)


def test_shared_with_gpio(fw, sgpio_cfg):
    pin = sgpio_cfg["out_pins"][0]
    fw.gpio.cfg(pin, "in")
    expect_error("busy", fw.sgpio.out, pin, 1)
    fw.gpio.release(pin)
    fw.sgpio.out(pin, 0)
    expect_error("busy", fw.gpio.cfg, pin, "in")


def test_errors(fw, board_cfg, sgpio_cfg):
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
