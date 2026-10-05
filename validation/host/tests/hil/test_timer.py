"""Timers (`hal::FreeRunningTimerStm`, `hal::TimerWithInterruptStm`) through the `tim` group: update rate on the
marker pin, interrupt counts, the free-running counter up and down, stop, and the timer shared with pwm, tpwm, qei
and the timer-triggered ADC.

Wiring set `bundle1`: `tests.timer.marker` (gpio0) on a DIO; the marker toggles once per update interrupt, so it runs
at half the update rate `timclk / ((prescaler + 1) (period + 1))`.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

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


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.matrix("timer.interrupt")
@pytest.mark.constraint(valid=dispatchable)
def test_update_marker(fw, ad3, need, board_cfg, timer_cfg, timer, irq, update):
    """The marker toggles on every update interrupt: it runs at half the update rate."""
    dio = need.dio(timer_cfg["marker"])
    index = timer["timer"]
    timclk = fw.tim.open(index, prescaler=update["prescaler"], period=update["period"], irq=irq, pin=timer_cfg["marker"])
    assert timclk == board_cfg.clock("timer")
    fw.tim.start(index)
    marker = timer_update_rate(timclk, update["prescaler"], update["period"]) / 2
    periods = timer_cfg["record_periods"]
    capture = ad3.logic.record_for(periods / marker, trigger=(dio, "rising"), timeout=periods / marker + 2)
    assert capture.frequency(dio) == pytest.approx(marker, rel=timer_cfg["tolerance"]["frequency"])


@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.matrix("timer.interrupt")
@pytest.mark.constraint(valid=dispatchable)
def test_interrupt_count(fw, timer_cfg, timer, irq, update):
    """`irqs` follows the update rate; dispatched callbacks at or below 1 kHz are not lost."""
    index = timer["timer"]
    timclk = fw.tim.open(index, prescaler=update["prescaler"], period=update["period"], irq=irq)
    rate = timer_update_rate(timclk, update["prescaler"], update["period"])
    fw.tim.start(index)
    after, before, elapsed = count_rate(fw, index, timer_cfg["window_s"])
    tolerance = timer_cfg["tolerance"]
    expected = rate * elapsed
    counted = after.irqs - before.irqs
    assert counted >= expected * (1 - tolerance["count"]) - rate * tolerance["latency_s"], (counted, expected)
    assert counted <= expected * (1 + tolerance["count"]) + 1, (counted, expected)


@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.board_params("mode", values=["up", "down"])
def test_free_running_counter(fw, timer_cfg, timer, mode):
    """`irq=none` counts at `timclk / (prescaler + 1)`, down from `period` with `mode=down`, without interrupts."""
    index = timer["timer"]
    free = timer_cfg["free_running"]
    if mode == "down" and index in timer_cfg["up_only"]:
        expect_error("unsupported", fw.tim.open, index, prescaler=free["prescaler"], period=free["period"], irq="none", mode=mode)
        return
    timclk = fw.tim.open(index, prescaler=free["prescaler"], period=free["period"], irq="none", mode=mode)
    fw.tim.start(index)
    after, before, elapsed = count_rate(fw, index, timer_cfg["window_s"])
    delta = after.cnt - before.cnt if mode == "up" else before.cnt - after.cnt
    tolerance = timer_cfg["tolerance"]
    rate = timclk / (free["prescaler"] + 1)
    assert 0 < delta <= rate * elapsed * (1 + tolerance["count"]) + 1, (delta, rate * elapsed)
    assert delta >= rate * (elapsed - 2 * tolerance["latency_s"]) * (1 - tolerance["count"]), (delta, rate * elapsed)
    assert (before.irqs, after.irqs) == (0, 0)


@pytest.mark.board_params("timer", "timer.timers")
@pytest.mark.board_params("irq", values=["immediate", "dispatched"])
def test_no_callback_before_the_first_update(fw, timer_cfg, timer, irq):
    """`tim.start` raises no update callback of its own: the update flag that `HAL_TIM_Base_Init` leaves on some HAL
    versions is cleared, so a timer with an update period of many seconds has `irqs=0` right after it starts."""
    index = timer["timer"]
    slow = timer_cfg["free_running"]
    fw.tim.open(index, prescaler=slow["prescaler"], period=slow["period"], irq=irq)
    fw.tim.start(index)
    assert fw.tim.count(index).irqs == 0


@pytest.mark.ad3
@pytest.mark.board_params("encoder", "qei.instances")
@pytest.mark.board_params("mode", values=["up", "down"])
def test_counts_the_requested_way_after_the_encoder(fw, ad3, need, board_cfg, timer_cfg, encoder, mode):
    """An encoder leaves its timer in encoder mode with CR1.DIR at its last direction, where DIR ignores writes:
    `tim.open` must still count the requested way. The encoder runs against `mode` before it closes."""
    index = encoder["index"]
    if index not in [entry["timer"] for entry in timer_cfg["timers"]]:
        pytest.skip(f"TIM{index} is not under test in `tests.timer.timers`")
    a, b = need.dio(encoder["a"]), need.dio(encoder["b"])
    fw.qei.open(index, a=encoder["a"], b=encoder["b"], res=board_cfg.param("qei.resolution"))
    ad3.pattern.quadrature(a, b, 1000, 10, "rev" if mode == "up" else "fwd")
    ad3.pattern.wait_done(timeout=3)
    fw.qei.close(index)
    free = timer_cfg["free_running"]
    fw.tim.open(index, prescaler=free["prescaler"], period=free["period"], irq="none", mode=mode)
    fw.tim.start(index)
    after, before, _ = count_rate(fw, index, timer_cfg["window_s"])
    delta = after.cnt - before.cnt if mode == "up" else before.cnt - after.cnt
    assert delta > 0, (mode, before.cnt, after.cnt)


@pytest.mark.board_params("timer", "timer.timers")
def test_stop_holds_the_counter(fw, timer_cfg, timer):
    """`tim.stop` freezes the counter and the interrupts; `tim.start` resumes; repeated start and stop are harmless."""
    index = timer["timer"]
    update = timer_cfg["interrupt"]["update"][0]
    fw.tim.open(index, prescaler=update["prescaler"], period=update["period"], irq="immediate")
    fw.tim.start(index)
    fw.tim.start(index)
    time.sleep(timer_cfg["window_s"])
    fw.tim.stop(index)
    fw.tim.stop(index)
    stopped = fw.tim.count(index)
    assert stopped.irqs > 0
    time.sleep(timer_cfg["window_s"])
    assert fw.tim.count(index) == stopped
    fw.tim.start(index)
    time.sleep(timer_cfg["window_s"])
    assert fw.tim.count(index).irqs > stopped.irqs


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer.timers")
def test_stop_stops_the_marker(fw, ad3, need, timer_cfg, timer):
    dio = need.dio(timer_cfg["marker"])
    index = timer["timer"]
    update = timer_cfg["interrupt"]["update"][1]
    timclk = fw.tim.open(index, prescaler=update["prescaler"], period=update["period"], irq="immediate", pin=timer_cfg["marker"])
    fw.tim.start(index)
    fw.tim.stop(index)
    marker = timer_update_rate(timclk, update["prescaler"], update["period"]) / 2
    capture = ad3.logic.record_for(timer_cfg["record_periods"] / marker)
    assert capture.frequency(dio) == 0.0
    assert len(set(capture.channel(dio))) == 1, "the marker moved after tim.stop"


@pytest.mark.board_params("timer", "timer.timers")
def test_marker_is_released_on_close(fw, need, timer_cfg, timer):
    """The marker pin belongs to the timer while it is open and returns to the pool with `tim.close`."""
    index, marker = timer["timer"], timer_cfg["marker"]
    need.unloaded(marker)
    fw.tim.open(index, irq="immediate", pin=marker)
    expect_error("busy", fw.gpio.cfg, marker, "in")
    fw.tim.close(index)
    expect_error("notopen", fw.tim.count, index)
    fw.gpio.cfg(marker, "in")
    expect_error("busy", fw.tim.open, index, irq="immediate", pin=marker)


def test_open_errors(fw, board_cfg, timer_cfg):
    """Argument errors in the protocol order: usage, range, pin, unsupported, then the update interrupt rate (range)."""
    first = timer_cfg["timers"][0]["timer"]
    wide = next((entry["timer"] for entry in timer_cfg["timers"] if timer_period_max(entry["timer"]) > 0xFFFF), None)
    narrow = next(entry["timer"] for entry in timer_cfg["timers"] if timer_period_max(entry["timer"]) == 0xFFFF)
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
    if wide is not None:
        assert fw.tim.open(wide, period=0xFFFFFFFF, irq="none") > 0
        fw.tim.close(wide)
    assert fw.tim.open(first, prescaler=0, period=1, irq="none") > 0, "the update interrupt rate limit needs an interrupt"
    fw.tim.close(first)
    for command in ("start", "stop", "count", "close"):
        expect_error("notopen", getattr(fw.tim, command), first)
    for line in ("tim.count", f"tim.count {first} {first}", f"tim.start {first} ch=1"):
        assert fw.terminal.command(line, check=False).reason == "usage", line
    assert fw.terminal.command(f"tim.open {first} pin=nosuchalias", check=False).reason == "pin"


def test_one_timer_at_a_time(fw, timer_cfg):
    first, second = (entry["timer"] for entry in timer_cfg["timers"][:2])
    fw.tim.open(first, irq="none")
    expect_error("busy", fw.tim.open, second, irq="none")
    expect_error("busy", fw.tim.open, first, irq="none")


@pytest.mark.board_params("timer", "timer.timers")
def test_timer_shared_with_other_groups(fw, board_cfg, timer):
    """A timer serves one group: while `tim` holds it, pwm, tpwm, the encoder and the ADC trigger get `ERR busy`,
    and `tim` gets it while pwm holds the timer."""
    index = timer["timer"]
    fw.tim.open(index, irq="none")
    expect_error("busy", fw.pwm.open, index, channels=[1])
    pwm_pin = next(entry for entry in board_cfg.param("timer_pwm.timers") if entry["timer"] == index)["pins"][0]
    expect_error("busy", fw.tpwm.open, index, pins=[pwm_pin])
    encoder = next((entry for entry in board_cfg.param("qei.instances") if entry["index"] == index), None)
    if encoder is not None:
        expect_error("busy", fw.qei.open, index, a=encoder["a"], b=encoder["b"])
    if index in expect.ADC_TRIGGER_TIMERS:
        expect_error("busy", fw.adc.open, board_cfg.param("adc.adc"), pins=[board_cfg.param("adc.inputs")[0]], timer=index)
    fw.tim.close(index)
    fw.pwm.open(index, channels=[1])
    expect_error("busy", fw.tim.open, index, irq="none")
