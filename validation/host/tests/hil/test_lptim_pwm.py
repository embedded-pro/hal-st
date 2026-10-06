"""LPTIM PWM (`hal::LpTimerPwmWithChannels<N>`, `hal::LpPwmChannelGpio`, STM32WBA55 only: LpTimerPwmStm is not
built for STM32WB) through the `lptpwm` group: frequency and duty per channel, `SetPulse`, an unused channel and the
argument checks.

Wiring set `bundle1`: LPTIM1 CH2 (PA15) and LPTIM2 CH1/CH2 (PA11/PA1) on DIOs. The tests assert the duty that
`lptpwm.duty` asks for (CCR = ARR * duty / 100, high while the counter is below CCR). The LPTIM drives the active
level from the compare match to the end of the period, so the driver selects the low output polarity; a measured
1 - duty (given in the assertion message) means the polarity is wrong. The LPTIM takes compare writes only while it
is enabled, so the tests set duties after `lptpwm.start`.

Scenarios: features/lptim_pwm.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.groups.timers import LPTIM_PERIOD_MAX, lptim_update_rate, pwm_duty_fraction


@pytest.fixture
def lptpwm_cfg(board_cfg):
    return board_cfg.param("lptim_pwm")


@pytest.fixture
def state():
    """What the steps of one scenario hand on to the later ones."""
    return {}


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def record(ad3, cfg, frequency):
    periods = cfg["record_periods"]
    return ad3.logic.record_for(periods / frequency, timeout=periods / frequency + 2)


def check_channel(capture, dio, frequency, fraction, cfg):
    assert capture.frequency(dio) == pytest.approx(frequency, rel=cfg["tolerance"]["frequency"]), f"DIO{dio} frequency"
    measured = capture.duty(dio)
    quantisation = 2 * frequency / capture.rate
    assert measured == pytest.approx(fraction, abs=cfg["tolerance"]["duty"] + quantisation), (
        f"DIO{dio}: high fraction {measured:.4f}, expected {fraction:.4f} (1 - duty would be {1 - fraction:.4f})"
    )


@pytest.mark.board_params("instance", "lptim_pwm.instances")
@pytest.mark.board_params("duty", "lptim_pwm.duties")
@scenario("lptim_pwm.feature", "Every wired channel runs at the update rate with the duty")
def test_duty(instance, duty):
    pass


@pytest.mark.board_params("instance", "lptim_pwm.instances")
@scenario("lptim_pwm.feature", "SetPulse sets the compare value and the shared auto-reload")
def test_pulse(instance):
    pass


@pytest.mark.board_params("instance", "lptim_pwm.instances")
@scenario("lptim_pwm.feature", "Stop stops the outputs and start resumes them")
def test_stop(instance):
    pass


@scenario("lptim_pwm.feature", "Invalid opens and commands are refused")
def test_open_errors():
    pass


@scenario("lptim_pwm.feature", "One LPTIM PWM runs at a time and claims its pins")
def test_one_lptim_pwm_at_a_time_and_pins_claimed():
    pass


@given("the channels of the instance with a pin are wired")
def channels_wired(need, state, instance):
    state["dios"] = [None if pin is None else need.dio(pin) for pin in instance["pins"]]


@given("the first channel of the instance with a pin is wired")
def first_channel_wired(need, state, instance):
    channel = next(position for position, pin in enumerate(instance["pins"], 1) if pin is not None)
    state.update(channel=channel, dio=need.dio(instance["pins"][channel - 1]))


@given("the first instance with a pin on channel 1")
def first_instance_on_channel_1(lptpwm_cfg, state):
    instance = next(entry for entry in lptpwm_cfg["instances"] if entry["pins"][0] is not None)
    state.update(index=instance["index"], pins=instance["pins"])


@given("the pins of the first instance are unloaded")
def first_instance_unloaded(need, lptpwm_cfg, state):
    first, second = lptpwm_cfg["instances"][:2]
    need.unloaded(*(pin for pin in first["pins"] if pin is not None))
    state.update(first=first, second=second, pin=next(pin for pin in first["pins"] if pin is not None))


@when("the instance is opened on its pins with the prescaler and period", target_fixture="lptimclk")
def open_instance(fw, lptpwm_cfg, instance):
    return fw.lptpwm.open(instance["index"], pins=instance["pins"], prescaler=lptpwm_cfg["prescaler"], period=lptpwm_cfg["period"])


@when("the instance starts")
def start_instance(fw, instance):
    fw.lptpwm.start(instance["index"])


@when("the instance stops")
def stop_instance(fw, instance):
    fw.lptpwm.stop(instance["index"])


@when("every wired channel gets the duty")
def set_duties(fw, state, instance, duty):
    for channel, dio in enumerate(state["dios"], 1):
        if dio is not None:
            fw.lptpwm.duty(instance["index"], channel, duty)


@when(parsers.parse("every channel with a pin gets {percent:d} % duty"))
def set_duty_of_pins(fw, instance, percent):
    for channel, pin in enumerate(instance["pins"], 1):
        if pin is not None:
            fw.lptpwm.duty(instance["index"], channel, percent)


@when("the channels are recorded at the update rate of the period")
def record_channels(ad3, lptpwm_cfg, state, lptimclk):
    state["frequency"] = lptim_update_rate(lptimclk, lptpwm_cfg["prescaler"], lptpwm_cfg["period"])
    state["capture"] = record(ad3, lptpwm_cfg, state["frequency"])


@when("the channel gets the compare value and period of the pulse")
def set_pulse(fw, lptpwm_cfg, state, instance):
    pulse = lptpwm_cfg["pulse"]
    fw.lptpwm.pulse(instance["index"], state["channel"], pulse["ccr"], pulse["period"])


@when("the instance opens on its pins")
def open_instance_plain(fw, state):
    fw.lptpwm.open(state["index"], pins=state["pins"])


@when("the first instance opens on its pins")
def open_first_instance(fw, state):
    fw.lptpwm.open(state["first"]["index"], pins=state["first"]["pins"])


@when("the first instance closes")
def close_first_instance(fw, state):
    fw.lptpwm.close(state["first"]["index"])


@then("it reports the LPTIM clock of the board")
def reports_lptim_clock(board_cfg, instance, lptimclk):
    assert lptimclk == board_cfg.clock("lptim", instance["index"])


@then("every wired channel runs at the update rate of the period with the duty")
def channels_match_duty(lptpwm_cfg, state, duty):
    for dio in state["dios"]:
        if dio is not None:
            check_channel(state["capture"], dio, state["frequency"], pwm_duty_fraction(lptpwm_cfg["period"], duty), lptpwm_cfg)


@then(
    "the channel runs at the update rate of the pulse period with a high fraction of the pulse compare value over the pulse period plus one"
)
def pulse_matches(ad3, lptpwm_cfg, state, lptimclk):
    pulse = lptpwm_cfg["pulse"]
    frequency = lptim_update_rate(lptimclk, lptpwm_cfg["prescaler"], pulse["period"])
    check_channel(record(ad3, lptpwm_cfg, frequency), state["dio"], frequency, pulse["ccr"] / (pulse["period"] + 1), lptpwm_cfg)


@then("no wired channel has more rising edges than a quarter of the record periods")
def outputs_stopped(lptpwm_cfg, state):
    capture, dios = state["capture"], [dio for dio in state["dios"] if dio is not None]
    stopped = lptpwm_cfg["record_periods"] // 4
    assert all(analysis.edge_count(capture.channel(dio), "rising") <= stopped for dio in dios), "an output runs after lptpwm.stop"


@then(parsers.parse("every wired channel runs at the update rate of the period with {percent:d} % duty"))
def channels_match_percent(lptpwm_cfg, state, percent):
    for dio in [dio for dio in state["dios"] if dio is not None]:
        check_channel(state["capture"], dio, state["frequency"], pwm_duty_fraction(lptpwm_cfg["period"], percent), lptpwm_cfg)


@then("every invalid open setting fails with its reason")
def open_settings_refused(fw, lptpwm_cfg, state):
    index, pins = state["index"], state["pins"]
    cases = [
        ({"pins": [None, None]}, "usage"),
        ({"pins": [*pins, pins[0]]}, "usage"),
        ({"pins": pins, "prescaler": 3}, "range"),
        ({"pins": pins, "prescaler": 256}, "range"),
        ({"pins": pins, "period": 0}, "range"),
        ({"pins": pins, "period": LPTIM_PERIOD_MAX + 1}, "range"),
        ({"pins": [lptpwm_cfg["foreign_pin"]]}, "pin"),
        ({"pins": list(reversed(pins))}, "pin"),
    ]
    for options, reason in cases:
        expect_error(reason, fw.lptpwm.open, index, **options)


@then(parsers.parse('opening without pins fails with "{usage_reason}" and with an unknown pin alias with "{pin_reason}"'))
def raw_opens_refused(fw, state, usage_reason, pin_reason):
    index = state["index"]
    for line, reason in ((f"lptpwm.open {index}", usage_reason), (f"lptpwm.open {index} pins=nosuchalias", pin_reason)):
        assert fw.terminal.command(line, check=False).reason == reason, line


@then(parsers.parse('opening each LPTIM the MCU lacks fails with "{reason}"'))
def missing_lptims_refused(fw, board_cfg, state, reason):
    for missing in board_cfg.param("lptim.missing"):
        expect_error(reason, fw.lptpwm.open, missing, pins=state["pins"])


@then(parsers.parse('duty, pulse, start, stop and close fail with "{reason}"'))
def closed_instance_refused(fw, state, reason):
    for command, args in (("duty", (1, 50)), ("pulse", (1, 1, 2)), ("start", ()), ("stop", ()), ("close", ())):
        expect_error(reason, getattr(fw.lptpwm, command), state["index"], *args)


@then(
    parsers.parse(
        'a duty on channel 3 or of 101 %, a pulse compare value beyond the period limit and a pulse period of 0 fail with "{reason}"'
    )
)
def duty_and_pulse_refused(fw, state, reason):
    for command, args in (("duty", (3, 50)), ("duty", (1, 101)), ("pulse", (1, LPTIM_PERIOD_MAX + 1, 2)), ("pulse", (1, 1, 0))):
        expect_error(reason, getattr(fw.lptpwm, command), state["index"], *args)


@then(parsers.parse('opening the second instance on its pins fails with "{reason}"'))
def second_instance_refused(fw, state, reason):
    second = state["second"]
    expect_error(reason, fw.lptpwm.open, second["index"], pins=second["pins"])


@then(parsers.parse('opening the first LPTIM as a timer without interrupt fails with "{reason}"'))
def lptim_refused(fw, state, reason):
    expect_error(reason, fw.lptim.open, state["first"]["index"], irq="none")


@then(parsers.parse('configuring the first pin of the first instance as a GPIO input fails with "{reason}"'))
def pin_claimed(fw, state, reason):
    expect_error(reason, fw.gpio.cfg, state["pin"], "in")


@then("the first pin of the first instance can be configured as a GPIO input")
def pin_released(fw, state):
    fw.gpio.cfg(state["pin"], "in")
