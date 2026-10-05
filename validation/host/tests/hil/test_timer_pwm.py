"""Timer PWM (`hal::TimerPwmWithChannels<N>`, `hal::PwmChannelGpio`) through the `tpwm` group: frequency and duty of
every channel, per-channel duties, `SetPulse` rewriting the period of every channel, unused (`-`) channels, starting
and stopping one channel or all, and the argument checks.

Wiring set `bundle1` (WBA55 TIM2 with CH3/CH4: `bundle2`): the channel pins of `tests.timer_pwm.timers` on DIOs.
`SetDuty` writes CCR = ARR * duty / 100 and the output is high while the counter is below CCR, so 100 % keeps one low
counter tick per period (`groups.timers.pwm_duty_fraction`); the YAML prescaler makes that tick 1 us long.

Scenarios: features/timer_pwm.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect
from hal_st_validation.groups.timers import TIMER_PRESCALER_MAX, pwm_duty_fraction, timer_period_max, timer_update_rate


@pytest.fixture
def tpwm_cfg(board_cfg):
    return board_cfg.param("timer_pwm")


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def wired(need, pins):
    """DIO per channel position (None for `-`); skips when a channel pin is not wired."""
    return [None if pin is None else need.dio(pin) for pin in pins]


def several_channels(values):
    timer = values.get("timer")
    return timer is None or sum(pin is not None for pin in timer["pins"]) >= 2


def record(ad3, cfg, frequency):
    periods = cfg["record_periods"]
    return ad3.logic.record_for(periods / frequency, timeout=periods / frequency + 2)


def check_channel(capture, dio, frequency, fraction, cfg):
    """`fraction` 0 is a static low output; anything else a waveform with that high fraction."""
    bits = capture.channel(dio)
    if fraction == 0:
        assert set(bits) == {0}, f"DIO{dio} moved at 0 %"
        return
    assert capture.frequency(dio) == pytest.approx(frequency, rel=cfg["tolerance"]["frequency"]), f"DIO{dio} frequency"
    quantisation = 2 * frequency / capture.rate
    assert capture.duty(dio) == pytest.approx(fraction, abs=cfg["tolerance"]["duty"] + quantisation), f"DIO{dio} duty"


def is_stopped(capture, dio, cfg):
    """No PWM on `dio`: a channel whose output is disabled is not driven (high impedance), so a few edges of a floating
    line are tolerated, far fewer than the `record_periods` of a running channel."""
    return analysis.edge_count(capture.channel(dio), "rising") <= cfg["record_periods"] // 4


def open_tpwm(fw, cfg, timer, pins=None):
    """Opens with the YAML prescaler and period; returns `timclk`."""
    return fw.tpwm.open(timer["timer"], pins=timer["pins"] if pins is None else pins, prescaler=cfg["prescaler"], period=cfg["period"])


@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.board_params("duty", "timer_pwm.duties")
@scenario("timer_pwm.feature", "Every channel runs at the update rate with the duty")
def test_duty(timer, duty):
    pass


@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.constraint(valid=several_channels)
@scenario("timer_pwm.feature", "Each channel keeps its own duty")
def test_channel_duties(timer):
    pass


@pytest.mark.board_params("timer", "timer_pwm.timers")
@scenario("timer_pwm.feature", "A pulse sets the period of every channel")
def test_pulse_sets_the_period_of_every_channel(timer):
    pass


@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.constraint(valid=several_channels)
@scenario("timer_pwm.feature", "An unused channel leaves its pin free")
def test_unused_channel(timer):
    pass


@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.constraint(valid=several_channels)
@scenario("timer_pwm.feature", "One channel or all start and stop")
def test_start_and_stop_one_channel(timer):
    pass


@pytest.mark.board_params("timer", "timer_pwm.timers")
@scenario("timer_pwm.feature", "The break input left enabled by pwm is disabled")
def test_break_left_by_pwm_is_disabled(timer):
    pass


@scenario("timer_pwm.feature", "Invalid open arguments are refused in the protocol order")
def test_open_errors():
    pass


@scenario("timer_pwm.feature", "Invalid channel arguments are refused")
def test_channel_errors():
    pass


@scenario("timer_pwm.feature", "The timer PWM holds its timer against pwm and tim")
def test_timer_shared_with_pwm_and_tim():
    pass


@scenario("timer_pwm.feature", "The channel pins are claimed while open")
def test_channel_pins_are_claimed():
    pass


@given("the channel pins are wired to DIOs", target_fixture="dios")
def pins_wired(need, timer):
    return wired(need, timer["pins"])


@given("the first used channel is left unused", target_fixture="layout")
def first_channel_unused(timer):
    first = next(position for position, pin in enumerate(timer["pins"]) if pin is not None)
    pins = list(timer["pins"])
    free = pins[first]
    pins[first] = None
    return pins, free


@given("the remaining channel pins are wired to DIOs", target_fixture="dios")
def remaining_pins_wired(need, layout):
    return wired(need, layout[0])


@given("the first two wired channels", target_fixture="two")
def first_two_channels(timer, dios):
    positions = [position for position, pin in enumerate(timer["pins"]) if pin is not None][:2]
    first, second = (dios[position] for position in positions)
    return first, second, positions[0] + 1


@given("a pwm break input on the timer", target_fixture="pwm_timer")
def pwm_break_input(board_cfg, timer):
    index = timer["timer"]
    pwm_timer = next((entry for entry in board_cfg.param("pwm.timers") if entry["timer"] == index and entry.get("brk")), None)
    if pwm_timer is None:
        pytest.skip(f"no pwm break input on TIM{index}")
    return pwm_timer


@given("a single-channel timer of the board file", target_fixture="single")
def single_channel_timer(tpwm_cfg):
    return next(entry for entry in tpwm_cfg["timers"] if entry["timer"] in expect.SINGLE_CHANNEL_TIMERS)


@given("the timer with the most channels", target_fixture="wide")
def widest_timer(tpwm_cfg):
    return max(tpwm_cfg["timers"], key=lambda entry: len(entry["pins"]))


@given("the timer with the most channels, its channel pins not loaded by an option", target_fixture="chosen")
def widest_timer_unloaded(need, tpwm_cfg):
    timer = max(tpwm_cfg["timers"], key=lambda entry: len(entry["pins"]))
    need.unloaded(*(pin for pin in timer["pins"] if pin is not None))
    return timer


@given("the first timer of the board file, its channel pins not loaded by an option", target_fixture="chosen")
def first_timer_unloaded(need, tpwm_cfg):
    timer = tpwm_cfg["timers"][0]
    need.unloaded(*(pin for pin in timer["pins"] if pin is not None))
    return timer


@given("the first two timers of the board file, the channel pins of the first not loaded by an option", target_fixture="pair")
def first_two_timers_unloaded(need, tpwm_cfg):
    first, second = tpwm_cfg["timers"][:2]
    need.unloaded(*(pin for pin in first["pins"] if pin is not None))
    return first, second


@when("the timer PWM opens with the prescaler and period of the board file", target_fixture="timclk")
def tpwm_opens(fw, tpwm_cfg, timer):
    return open_tpwm(fw, tpwm_cfg, timer)


@when("the timer PWM opens with those pins and the prescaler and period of the board file", target_fixture="timclk")
def tpwm_opens_with_layout(fw, tpwm_cfg, timer, layout):
    return open_tpwm(fw, tpwm_cfg, timer, layout[0])


@when("every wired channel is set to the duty")
def wired_channels_to_duty(fw, timer, dios, duty):
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            fw.tpwm.duty(timer["timer"], channel, duty)


@when(parsers.parse("every wired channel is set to {percent:d} %"))
def wired_channels_to(fw, timer, dios, percent):
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            fw.tpwm.duty(timer["timer"], channel, percent)


@when("each channel is set to its channel duty of the board file")
def channels_to_their_duties(fw, tpwm_cfg, timer, dios):
    duties = tpwm_cfg["channel_duties"]
    for channel in range(1, len(dios) + 1):
        fw.tpwm.duty(timer["timer"], channel, duties[channel - 1])


@when(parsers.parse("every channel is set to {percent:d} %"))
def channels_to(fw, timer, dios, percent):
    for channel in range(1, len(dios) + 1):
        fw.tpwm.duty(timer["timer"], channel, percent)


@when("the timer PWM starts")
def tpwm_starts(fw, timer):
    fw.tpwm.start(timer["timer"])


@when("the timer PWM stops twice")
def tpwm_stops_twice(fw, timer):
    fw.tpwm.stop(timer["timer"])
    fw.tpwm.stop(timer["timer"])


@when("the first of them starts twice")
def first_channel_starts_twice(fw, timer, two):
    fw.tpwm.start(timer["timer"], ch=two[2])
    fw.tpwm.start(timer["timer"], ch=two[2])


@when("the first of them stops")
def first_channel_stops(fw, timer, two):
    fw.tpwm.stop(timer["timer"], ch=two[2])


@when(parsers.parse("channel {channel:d} gets the pulse of the board file"))
def channel_gets_pulse(fw, tpwm_cfg, timer, channel):
    pulse = tpwm_cfg["pulse"]
    fw.tpwm.pulse(timer["timer"], channel, pulse["ccr"], pulse["period"])


@when("the pin of the unused channel is configured as an input")
def unused_pin_input(fw, layout):
    fw.gpio.cfg(layout[1], "in")


@when("the channels are recorded", target_fixture="capture")
def channels_recorded(ad3, tpwm_cfg, timclk):
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    return record(ad3, tpwm_cfg, frequency)


@when("the channels are recorded at the update rate of the pulse period", target_fixture="capture")
def channels_recorded_at_pulse(ad3, tpwm_cfg, timclk):
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["pulse"]["period"])
    return record(ad3, tpwm_cfg, frequency)


@when("pwm opens on the first pwm channel of the timer with the break input active low")
def pwm_opens_with_break(fw, timer, pwm_timer):
    fw.pwm.open(timer["timer"], pins=[pwm_timer["channels"][0]["pin"]], brk=pwm_timer["brk"], brkpol="low")


@when("pwm closes on the timer")
def pwm_closes(fw, timer):
    fw.pwm.close(timer["timer"])


@given("its first channel pin", target_fixture="channel_pin")
def chosen_first_pin(chosen):
    return next(pin for pin in chosen["pins"] if pin is not None)


@when("the timer PWM opens on it with its channel pins")
def chosen_opens(fw, chosen):
    fw.tpwm.open(chosen["timer"], pins=chosen["pins"])


@when("the timer PWM closes on it")
def chosen_closes(fw, chosen):
    fw.tpwm.close(chosen["timer"])


@when("its first channel pin is configured as an input")
def chosen_pin_input(fw, channel_pin):
    fw.gpio.cfg(channel_pin, "in")


@when("the timer PWM opens on the first of them with its channel pins")
def first_of_pair_opens(fw, pair):
    fw.tpwm.open(pair[0]["timer"], pins=pair[0]["pins"])


@when("the timer PWM closes on the first of them")
def first_of_pair_closes(fw, pair):
    fw.tpwm.close(pair[0]["timer"])


@when("tim opens on the first of them without interrupts")
def tim_opens_on_first_of_pair(fw, pair):
    fw.tim.open(pair[0]["timer"], irq="none")


@then("it reports the timer clock of the board file")
def reports_timer_clock(board_cfg, timclk):
    assert timclk == board_cfg.clock("timer")


@then("every wired channel runs at the update rate with the high fraction of the duty")
def wired_channels_run_at_duty(tpwm_cfg, dios, timclk, capture, duty):
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    for dio in dios:
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(tpwm_cfg["period"], duty), tpwm_cfg)


@then(parsers.parse("every wired channel runs at the update rate with the high fraction of {percent:d} %"))
def wired_channels_run_at(tpwm_cfg, dios, timclk, capture, percent):
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    for dio in dios:
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(tpwm_cfg["period"], percent), tpwm_cfg)


@then("every wired channel runs at the update rate with the high fraction of its channel duty")
def wired_channels_run_at_their_duties(tpwm_cfg, dios, timclk, capture):
    duties = tpwm_cfg["channel_duties"]
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(tpwm_cfg["period"], duties[channel - 1]), tpwm_cfg)


@then(
    parsers.parse(
        "channel {pulsed:d} runs with the compare value of the pulse and every other wired channel with the compare value of "
        "{percent:d} %, all at the update rate of the pulse period"
    )
)
def pulse_period_on_every_channel(tpwm_cfg, dios, timclk, capture, pulsed, percent):
    pulse = tpwm_cfg["pulse"]
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], pulse["period"])
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            ccr = pulse["ccr"] if channel == pulsed else tpwm_cfg["period"] * percent // 100
            check_channel(capture, dio, frequency, ccr / (pulse["period"] + 1), tpwm_cfg)


@then(parsers.parse("the first of them runs at the update rate with the high fraction of {percent:d} %"))
def first_channel_runs(tpwm_cfg, timclk, two, capture, percent):
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    check_channel(capture, two[0], frequency, pwm_duty_fraction(tpwm_cfg["period"], percent), tpwm_cfg)


@then(parsers.parse("the second of them runs at the update rate with the high fraction of {percent:d} %"))
def second_channel_runs(tpwm_cfg, timclk, two, capture, percent):
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    check_channel(capture, two[1], frequency, pwm_duty_fraction(tpwm_cfg["period"], percent), tpwm_cfg)


@then("the second of them, which was not started, does not run")
def second_channel_not_started(tpwm_cfg, two, capture):
    assert is_stopped(capture, two[1], tpwm_cfg), "a channel that was not started runs"


@then("the first of them, which was stopped, does not run")
def first_channel_stopped(tpwm_cfg, two, capture):
    assert is_stopped(capture, two[0], tpwm_cfg), "the stopped channel runs"


@then("neither of them runs")
def neither_runs(tpwm_cfg, two, capture):
    assert is_stopped(capture, two[0], tpwm_cfg) and is_stopped(capture, two[1], tpwm_cfg)


@then("every invalid open of it is refused with its reason in the protocol order")
def open_errors(fw, tpwm_cfg, board_cfg, single):
    index, pin = single["timer"], single["pins"][0]
    cases = [
        ((index,), {"pins": [None]}, "usage"),
        ((index,), {"pins": [pin] * 5}, "usage"),
        ((index,), {"pins": [pin], "prescaler": TIMER_PRESCALER_MAX + 1}, "range"),
        ((index,), {"pins": [pin], "period": 0}, "range"),
        ((index,), {"pins": [pin], "period": timer_period_max(index) + 1}, "range"),
        ((index,), {"pins": [tpwm_cfg["foreign_pin"]]}, "pin"),
        ((index,), {"pins": [pin, None]}, "unsupported"),
    ]
    cases += [((missing,), {"pins": [pin]}, "range") for missing in board_cfg.param("timer.missing")]
    for args, options, reason in cases:
        expect_error(reason, fw.tpwm.open, *args, **options)


@then("the malformed open command lines of it are refused with their reason")
def malformed_open_lines_refused(fw, single):
    index = single["timer"]
    raw = {f"tpwm.open {index}": "usage", f"tpwm.open {index} pins=,": "usage", f"tpwm.open {index} pins=nosuchalias": "pin"}
    for line, reason in raw.items():
        assert fw.terminal.command(line, check=False).reason == reason, line


@then(
    parsers.parse(
        'opening the timer with the most channels with its channel pins reversed fails with "{reason}", if it has several '
        "and reversing them changes them and leaves a pin first"
    )
)
def reversed_pins_refused(fw, wide, reason):
    if len(wide["pins"]) > 1:
        reversed_pins = list(reversed(wide["pins"]))
        if reversed_pins != wide["pins"] and reversed_pins[0] is not None:
            expect_error(reason, fw.tpwm.open, wide["timer"], pins=reversed_pins)


@then(parsers.parse('setting a duty or a pulse, starting, stopping and closing it fail with "{reason}"'))
def unopened_commands_refused(fw, single, reason):
    for command, args in (("duty", (1, 50)), ("pulse", (1, 1, 2)), ("start", ()), ("stop", ()), ("close", ())):
        expect_error(reason, getattr(fw.tpwm, command), single["timer"], *args)


@then('every out-of-range duty and pulse is refused with "range"')
def channel_errors(fw, chosen):
    index, channels = chosen["timer"], len(chosen["pins"])
    maximum = timer_period_max(index)
    cases = [
        ("duty", (0, 50), "range"),
        ("duty", (channels + 1, 50), "range"),
        ("duty", (1, 101), "range"),
        ("pulse", (1, 0, 0), "range"),
        ("pulse", (channels + 1, 1, 2), "range"),
    ]
    if maximum < 0xFFFFFFFF:
        cases += [("pulse", (1, maximum + 1, 2), "range"), ("pulse", (1, 1, maximum + 1), "range")]
    for command, args, reason in cases:
        expect_error(reason, getattr(fw.tpwm, command), index, *args)


@then(parsers.parse('starting the channel after the last one fails with "{reason}"'))
def start_past_last_refused(fw, chosen, reason):
    expect_error(reason, fw.tpwm.start, chosen["timer"], ch=len(chosen["pins"]) + 1)


@then(parsers.parse('stopping channel {channel:d} fails with "{reason}"'))
def stop_channel_refused(fw, chosen, channel, reason):
    expect_error(reason, fw.tpwm.stop, chosen["timer"], ch=channel)


@then(parsers.parse('the malformed channel command lines are refused with "{reason}"'))
def malformed_channel_lines_refused(fw, chosen, reason):
    index = chosen["timer"]
    for line in (f"tpwm.duty {index} 1", f"tpwm.start {index} ch=x", f"tpwm.stop {index} channel=1", f"tpwm.duty {index} 1 50 1"):
        assert fw.terminal.command(line, check=False).reason == reason, line


@then(parsers.parse("the last channel takes a duty of {percent:d} % and a pulse with the largest compare value and period"))
def last_channel_takes_limits(fw, chosen, percent):
    index, channels = chosen["timer"], len(chosen["pins"])
    maximum = timer_period_max(index)
    fw.tpwm.duty(index, channels, percent)
    fw.tpwm.pulse(index, channels, maximum, maximum)


@then(parsers.parse('opening pwm on the first of them with channel {channel:d} fails with "{reason}"'))
def pwm_refused(fw, pair, channel, reason):
    expect_error(reason, fw.pwm.open, pair[0]["timer"], channels=[channel])


@then(parsers.parse('opening tim on the first of them without interrupts fails with "{reason}"'))
def tim_refused(fw, pair, reason):
    expect_error(reason, fw.tim.open, pair[0]["timer"], irq="none")


@then(parsers.parse('opening the timer PWM on the second of them with its channel pins fails with "{reason}"'))
def second_of_pair_refused(fw, pair, reason):
    expect_error(reason, fw.tpwm.open, pair[1]["timer"], pins=pair[1]["pins"])


@then(parsers.parse('opening the timer PWM on the first of them with its channel pins fails with "{reason}"'))
def first_of_pair_refused(fw, pair, reason):
    expect_error(reason, fw.tpwm.open, pair[0]["timer"], pins=pair[0]["pins"])


@then(parsers.parse('configuring its first channel pin as an input fails with "{reason}"'))
def chosen_pin_refused(fw, channel_pin, reason):
    expect_error(reason, fw.gpio.cfg, channel_pin, "in")


@then(parsers.parse('opening the timer PWM on it with its channel pins fails with "{reason}"'))
def chosen_open_refused(fw, chosen, reason):
    expect_error(reason, fw.tpwm.open, chosen["timer"], pins=chosen["pins"])
