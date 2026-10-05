"""Timers (`hal::FreeRunningTimerStm`, `hal::TimerWithInterruptStm`) through the `tim` group: update rate on the
marker pin, interrupt counts, the free-running counter up and down, stop, and the timer shared with pwm, tpwm, qei
and the timer-triggered ADC.

Wiring set `bundle1`: `tests.timer.marker` (gpio0) on a DIO; the marker toggles once per update interrupt, so it runs
at half the update rate `timclk / ((prescaler + 1) (period + 1))`.

Scenarios: features/timer.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect
from hal_st_validation.groups.timers import TIMER_PRESCALER_MAX, timer_period_max, timer_update_rate


@pytest.fixture
def timer_cfg(board_cfg):
    return board_cfg.param("timer")


def dispatchable(values):
    """Dispatched callbacks coalesce above 1 kHz: those rates run with `irq=immediate` only."""
    update = values.get("update")
    return values.get("irq") != "dispatched" or update is None or update.get("dispatched", True)


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def count_rate(fw, index, window):
    """Counter and interrupt deltas over about `window` seconds, with the host time between the two readings."""
    start = time.monotonic()
    before = fw.tim.count(index)
    time.sleep(window)
    after = fw.tim.count(index)
    return after, before, time.monotonic() - start


@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.matrix("timer.interrupt")
@pytest.mark.constraint(valid=dispatchable)
@scenario("timer.feature", "The marker runs at half the update rate")
def test_update_marker(timer, irq, update):
    pass


@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.matrix("timer.interrupt")
@pytest.mark.constraint(valid=dispatchable)
@scenario("timer.feature", "The interrupt count follows the update rate")
def test_interrupt_count(timer, irq, update):
    pass


@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.board_params("mode", values=["up", "down"])
@scenario("timer.feature", "The free-running counter counts up or down without interrupts")
def test_free_running_counter(timer, mode):
    pass


@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.board_params("irq", values=["immediate", "dispatched"])
@scenario("timer.feature", "Starting raises no update callback of its own")
def test_no_callback_before_the_first_update(timer, irq):
    pass


@pytest.mark.board_params("encoder", "qei.instances")
@pytest.mark.board_params("mode", values=["up", "down"])
@scenario("timer.feature", "The timer counts the requested way after the encoder")
def test_counts_the_requested_way_after_the_encoder(encoder, mode):
    pass


@pytest.mark.board_params("timer", "timer.timers")
@scenario("timer.feature", "Stop holds the counter and start resumes it")
def test_stop_holds_the_counter(timer):
    pass


@pytest.mark.board_params("timer", "timer.timers")
@scenario("timer.feature", "Stop stops the marker")
def test_stop_stops_the_marker(timer):
    pass


@pytest.mark.board_params("timer", "timer.timers")
@scenario("timer.feature", "The marker pin is released on close")
def test_marker_is_released_on_close(timer):
    pass


@scenario("timer.feature", "Invalid open arguments are refused in the protocol order")
def test_open_errors():
    pass


@scenario("timer.feature", "One timer is open at a time")
def test_one_timer_at_a_time():
    pass


@pytest.mark.board_params("timer", "timer.timers")
@scenario("timer.feature", "A timer serves one group")
def test_timer_shared_with_other_groups(timer):
    pass


@given("the marker pin is wired to a DIO", target_fixture="dio")
def marker_wired(need, timer_cfg):
    return need.dio(timer_cfg["marker"])


@given("the marker pin is not loaded by an option")
def marker_unloaded(need, timer_cfg):
    need.unloaded(timer_cfg["marker"])


@given("the encoder timer is one of the timers under test")
def encoder_timer_under_test(timer_cfg, encoder):
    index = encoder["index"]
    if index not in [entry["timer"] for entry in timer_cfg["timers"]]:
        pytest.skip(f"TIM{index} is not under test in `tests.timer.timers`")


@given("the encoder inputs are wired to DIOs", target_fixture="encoder_dios")
def encoder_wired(need, encoder):
    return need.dio(encoder["a"]), need.dio(encoder["b"])


@given("the first timer under test", target_fixture="first")
def first_timer(timer_cfg):
    return timer_cfg["timers"][0]["timer"]


@given("a timer with a period wider than 16 bits, if any", target_fixture="wide")
def wide_timer(timer_cfg):
    return next((entry["timer"] for entry in timer_cfg["timers"] if timer_period_max(entry["timer"]) > 0xFFFF), None)


@given("a timer with a 16-bit period", target_fixture="narrow")
def narrow_timer(timer_cfg):
    return next(entry["timer"] for entry in timer_cfg["timers"] if timer_period_max(entry["timer"]) == 0xFFFF)


@given("the first two timers under test", target_fixture="pair")
def first_two_timers(timer_cfg):
    first, second = (entry["timer"] for entry in timer_cfg["timers"][:2])
    return first, second


@when("the timer opens with the update prescaler and period, the interrupt mode and the marker pin", target_fixture="timclk")
def open_update_with_marker(fw, timer_cfg, timer, irq, update):
    return fw.tim.open(timer["timer"], prescaler=update["prescaler"], period=update["period"], irq=irq, pin=timer_cfg["marker"])


@when("the timer opens with the update prescaler and period and the interrupt mode", target_fixture="timclk")
def open_update(fw, timer, irq, update):
    return fw.tim.open(timer["timer"], prescaler=update["prescaler"], period=update["period"], irq=irq)


@when(
    'the timer opens free-running without interrupts, counting the mode way, or fails with "unsupported" when it counts '
    "down and the timer counts up only",
    target_fixture="timclk",
)
def open_free_running_or_up_only(fw, timer_cfg, timer, mode):
    index = timer["timer"]
    free = timer_cfg["free_running"]
    if mode == "down" and index in timer_cfg["up_only"]:
        expect_error("unsupported", fw.tim.open, index, prescaler=free["prescaler"], period=free["period"], irq="none", mode=mode)
        return None
    return fw.tim.open(index, prescaler=free["prescaler"], period=free["period"], irq="none", mode=mode)


@when("the timer opens with the free-running prescaler and period and the interrupt mode")
def open_slow(fw, timer_cfg, timer, irq):
    slow = timer_cfg["free_running"]
    fw.tim.open(timer["timer"], prescaler=slow["prescaler"], period=slow["period"], irq=irq)


@when("the timer opens with the first interrupt update setting and immediate interrupts")
def open_first_update(fw, timer_cfg, timer):
    update = timer_cfg["interrupt"]["update"][0]
    fw.tim.open(timer["timer"], prescaler=update["prescaler"], period=update["period"], irq="immediate")


@when("the timer opens with the second interrupt update setting, immediate interrupts and the marker pin", target_fixture="timclk")
def open_second_update_with_marker(fw, timer_cfg, timer):
    update = timer_cfg["interrupt"]["update"][1]
    return fw.tim.open(timer["timer"], prescaler=update["prescaler"], period=update["period"], irq="immediate", pin=timer_cfg["marker"])


@when("the timer opens with immediate interrupts and the marker pin")
def open_with_marker(fw, timer_cfg, timer):
    fw.tim.open(timer["timer"], irq="immediate", pin=timer_cfg["marker"])


@when("the timer opens without interrupts")
def open_without_interrupts(fw, timer):
    fw.tim.open(timer["timer"], irq="none")


@when("the timer starts")
def start(fw, timer):
    fw.tim.start(timer["timer"])


@when("the timer starts, if it opened")
def start_if_opened(fw, timer, timclk):
    if timclk is not None:
        fw.tim.start(timer["timer"])


@when("the timer starts twice")
def start_twice(fw, timer):
    fw.tim.start(timer["timer"])
    fw.tim.start(timer["timer"])


@when("the timer stops")
def stop(fw, timer):
    fw.tim.stop(timer["timer"])


@when("the timer stops twice")
def stop_twice(fw, timer):
    fw.tim.stop(timer["timer"])
    fw.tim.stop(timer["timer"])


@when("the timer closes")
def close(fw, timer):
    fw.tim.close(timer["timer"])


@when("the host waits the counting window")
def host_waits(timer_cfg):
    time.sleep(timer_cfg["window_s"])


@when("the counts are read over the counting window", target_fixture="counts")
def counts_over_window(fw, timer_cfg, timer):
    return count_rate(fw, timer["timer"], timer_cfg["window_s"])


@when("the counts are read over the counting window, if the timer opened", target_fixture="counts")
def counts_over_window_if_opened(fw, timer_cfg, timer, timclk):
    if timclk is None:
        return None
    return count_rate(fw, timer["timer"], timer_cfg["window_s"])


@when("the counts are read", target_fixture="stopped")
def counts_read(fw, timer):
    return fw.tim.count(timer["timer"])


@when("the marker pin is configured as an input")
def marker_input(fw, timer_cfg):
    fw.gpio.cfg(timer_cfg["marker"], "in")


@when("the encoder opens with the resolution of the board file")
def encoder_opens(fw, board_cfg, encoder):
    fw.qei.open(encoder["index"], a=encoder["a"], b=encoder["b"], res=board_cfg.param("qei.resolution"))


@when(parsers.parse("the AD3 turns the encoder {cycles:d} cycles at {frequency:d} Hz against the mode"))
def encoder_turns(ad3, encoder_dios, mode, cycles, frequency):
    a, b = encoder_dios
    ad3.pattern.quadrature(a, b, frequency, cycles, "rev" if mode == "up" else "fwd")
    ad3.pattern.wait_done(timeout=3)


@when("the encoder closes")
def encoder_closes(fw, encoder):
    fw.qei.close(encoder["index"])


@when("the timer of the encoder opens free-running without interrupts, counting the mode way")
def encoder_timer_opens(fw, timer_cfg, encoder, mode):
    free = timer_cfg["free_running"]
    fw.tim.open(encoder["index"], prescaler=free["prescaler"], period=free["period"], irq="none", mode=mode)


@when("the timer of the encoder starts")
def encoder_timer_starts(fw, encoder):
    fw.tim.start(encoder["index"])


@when("the counts of the timer of the encoder are read over the counting window", target_fixture="counts")
def encoder_timer_counts(fw, timer_cfg, encoder):
    return count_rate(fw, encoder["index"], timer_cfg["window_s"])


@when("the first of them opens without interrupts")
def first_of_pair_opens(fw, pair):
    fw.tim.open(pair[0], irq="none")


@when(parsers.parse("pwm opens on the timer with channel {channel:d}"))
def pwm_opens(fw, timer, channel):
    fw.pwm.open(timer["timer"], channels=[channel])


@then("it reports the timer clock of the board file")
def reports_timer_clock(board_cfg, timclk):
    assert timclk == board_cfg.clock("timer")


@then("the marker runs at half the update rate within the frequency tolerance")
def marker_runs(ad3, timer_cfg, update, dio, timclk):
    marker = timer_update_rate(timclk, update["prescaler"], update["period"]) / 2
    periods = timer_cfg["record_periods"]
    capture = ad3.logic.record_for(periods / marker, trigger=(dio, "rising"), timeout=periods / marker + 2)
    assert capture.frequency(dio) == pytest.approx(marker, rel=timer_cfg["tolerance"]["frequency"])


@then("the interrupts counted match the update rate within the count tolerance and the interrupt latency")
def interrupts_match_rate(timer_cfg, update, timclk, counts):
    rate = timer_update_rate(timclk, update["prescaler"], update["period"])
    after, before, elapsed = counts
    tolerance = timer_cfg["tolerance"]
    expected = rate * elapsed
    counted = after.irqs - before.irqs
    assert counted >= expected * (1 - tolerance["count"]) - rate * tolerance["latency_s"], (counted, expected)
    assert counted <= expected * (1 + tolerance["count"]) + 1, (counted, expected)


@then(
    "the counter moved the mode way at the timer clock over the free-running prescaler + 1 within the count tolerance and "
    "the latency, without interrupts, if the timer opened"
)
def counter_moved(timer_cfg, mode, timclk, counts):
    if timclk is None:
        return
    after, before, elapsed = counts
    delta = after.cnt - before.cnt if mode == "up" else before.cnt - after.cnt
    tolerance = timer_cfg["tolerance"]
    rate = timclk / (timer_cfg["free_running"]["prescaler"] + 1)
    assert 0 < delta <= rate * elapsed * (1 + tolerance["count"]) + 1, (delta, rate * elapsed)
    assert delta >= rate * (elapsed - 2 * tolerance["latency_s"]) * (1 - tolerance["count"]), (delta, rate * elapsed)
    assert (before.irqs, after.irqs) == (0, 0)


@then("the timer has counted no interrupt")
def no_interrupt(fw, timer):
    assert fw.tim.count(timer["timer"]).irqs == 0


@then("the counter moved the mode way")
def moved_the_mode_way(mode, counts):
    after, before, _ = counts
    delta = after.cnt - before.cnt if mode == "up" else before.cnt - after.cnt
    assert delta > 0, (mode, before.cnt, after.cnt)


@then("interrupts have been counted")
def interrupts_counted(stopped):
    assert stopped.irqs > 0


@then("the counts read as before")
def counts_as_before(fw, timer, stopped):
    assert fw.tim.count(timer["timer"]) == stopped


@then("more interrupts have been counted than before")
def more_interrupts(fw, timer, stopped):
    assert fw.tim.count(timer["timer"]).irqs > stopped.irqs


@then("the marker stays still over the record periods at half the update rate")
def marker_still(ad3, timer_cfg, dio, timclk):
    update = timer_cfg["interrupt"]["update"][1]
    marker = timer_update_rate(timclk, update["prescaler"], update["period"]) / 2
    capture = ad3.logic.record_for(timer_cfg["record_periods"] / marker)
    assert capture.frequency(dio) == 0.0
    assert len(set(capture.channel(dio))) == 1, "the marker moved after tim.stop"


@then(parsers.parse('configuring the marker pin as an input fails with "{reason}"'))
def marker_input_refused(fw, timer_cfg, reason):
    expect_error(reason, fw.gpio.cfg, timer_cfg["marker"], "in")


@then(parsers.parse('reading the counts of the timer fails with "{reason}"'))
def count_refused(fw, timer, reason):
    expect_error(reason, fw.tim.count, timer["timer"])


@then(parsers.parse('opening the timer with immediate interrupts and the marker pin fails with "{reason}"'))
def open_with_marker_refused(fw, timer_cfg, timer, reason):
    expect_error(reason, fw.tim.open, timer["timer"], irq="immediate", pin=timer_cfg["marker"])


@then("every invalid open is refused with its reason in the protocol order")
def open_errors(fw, board_cfg, timer_cfg, first, narrow):
    marker = timer_cfg["marker"]
    cases = [
        ((first,), {"irq": "nmi"}, "usage"),
        ((first,), {"mode": "sideways"}, "usage"),
        ((first,), {"irq": "none", "pin": marker}, "usage"),
        ((first,), {"prescaler": TIMER_PRESCALER_MAX + 1}, "range"),
        ((first,), {"period": 0}, "range"),
        ((narrow,), {"period": 0x10000}, "range"),
        ((first,), {"pin": board_cfg.param("system.unbonded_pins")[0]}, "pin"),
        ((first,), {"mode": "down"}, "unsupported"),
        ((first,), {"prescaler": 0, "period": 1}, "range"),
        ((first,), {"prescaler": 0, "period": 1, "irq": "immediate"}, "range"),
    ]
    cases += [((timer,), {}, "range") for timer in timer_cfg["missing"]]
    cases += [((timer,), {"irq": "none", "mode": "down"}, "unsupported") for timer in timer_cfg["up_only"]]
    for args, options, reason in cases:
        expect_error(reason, fw.tim.open, *args, **options)


@then("the timer with the wider period opens with a period of 0xFFFFFFFF without interrupts and closes, if there is one")
def wide_period_opens(fw, wide):
    if wide is not None:
        assert fw.tim.open(wide, period=0xFFFFFFFF, irq="none") > 0
        fw.tim.close(wide)


@then(parsers.parse("the first timer opens with prescaler {prescaler:d} and period {period:d} without interrupts and closes"))
def fastest_opens_without_interrupts(fw, first, prescaler, period):
    assert fw.tim.open(first, prescaler=prescaler, period=period, irq="none") > 0, "the update interrupt rate limit needs an interrupt"
    fw.tim.close(first)


@then(parsers.parse('starting, stopping, reading and closing the closed first timer fail with "{reason}"'))
def closed_commands_refused(fw, first, reason):
    for command in ("start", "stop", "count", "close"):
        expect_error(reason, getattr(fw.tim, command), first)


@then(parsers.parse('the malformed command lines are refused with "{reason}"'))
def malformed_lines_refused(fw, first, reason):
    for line in ("tim.count", f"tim.count {first} {first}", f"tim.start {first} ch=1"):
        assert fw.terminal.command(line, check=False).reason == reason, line


@then(parsers.parse('opening the first timer with an unknown pin alias fails with "{reason}"'))
def unknown_alias_refused(fw, first, reason):
    assert fw.terminal.command(f"tim.open {first} pin=nosuchalias", check=False).reason == reason


@then(parsers.parse('opening the second of them without interrupts fails with "{reason}"'))
def second_of_pair_refused(fw, pair, reason):
    expect_error(reason, fw.tim.open, pair[1], irq="none")


@then(parsers.parse('opening the first of them again without interrupts fails with "{reason}"'))
def first_of_pair_refused(fw, pair, reason):
    expect_error(reason, fw.tim.open, pair[0], irq="none")


@then(parsers.parse('opening pwm on the timer with channel {channel:d} fails with "{reason}"'))
def pwm_refused(fw, timer, channel, reason):
    expect_error(reason, fw.pwm.open, timer["timer"], channels=[channel])


@then(parsers.parse('opening tpwm on the timer with its first timer PWM pin fails with "{reason}"'))
def tpwm_refused(fw, board_cfg, timer, reason):
    index = timer["timer"]
    pwm_pin = next(entry for entry in board_cfg.param("timer_pwm.timers") if entry["timer"] == index)["pins"][0]
    expect_error(reason, fw.tpwm.open, index, pins=[pwm_pin])


@then(parsers.parse('opening the encoder on the timer fails with "{reason}", if the timer has one'))
def encoder_refused(fw, board_cfg, timer, reason):
    index = timer["timer"]
    encoder = next((entry for entry in board_cfg.param("qei.instances") if entry["index"] == index), None)
    if encoder is not None:
        expect_error(reason, fw.qei.open, index, a=encoder["a"], b=encoder["b"])


@then(parsers.parse('opening the ADC triggered by the timer fails with "{reason}", if the timer can trigger it'))
def adc_trigger_refused(fw, board_cfg, timer, reason):
    index = timer["timer"]
    if index in expect.ADC_TRIGGER_TIMERS:
        expect_error(reason, fw.adc.open, board_cfg.param("adc.adc"), pins=[board_cfg.param("adc.inputs")[0]], timer=index)


@then(parsers.parse('opening the timer without interrupts fails with "{reason}"'))
def open_without_interrupts_refused(fw, timer, reason):
    expect_error(reason, fw.tim.open, timer["timer"], irq="none")
