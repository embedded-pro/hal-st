"""Timer PWM (`hal::TimerPwmWithChannels<N>`, `hal::PwmChannelGpio`) through the `tpwm` group: frequency and duty of
every channel, per-channel duties, `SetPulse` rewriting the period of every channel, unused (`-`) channels, starting
and stopping one channel or all, and the argument checks.

Wiring set `bundle1` (WBA55 TIM2 with CH3/CH4: `bundle2`): the channel pins of `tests.timer_pwm.timers` on DIOs.
`SetDuty` writes CCR = ARR * duty / 100 and the output is high while the counter is below CCR, so 100 % keeps one low
counter tick per period (`groups.timers.pwm_duty_fraction`); the YAML prescaler makes that tick 1 us long.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

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


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.board_params("duty", "timer_pwm.duties")
def test_duty(fw, ad3, need, board_cfg, tpwm_cfg, timer, duty):
    """One duty on every channel: frequency timclk / ((prescaler + 1) (period + 1)), high fraction of `SetDuty`."""
    dios = wired(need, timer["pins"])
    timclk = open_tpwm(fw, tpwm_cfg, timer)
    assert timclk == board_cfg.clock("timer")
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            fw.tpwm.duty(timer["timer"], channel, duty)
    fw.tpwm.start(timer["timer"])
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    capture = record(ad3, tpwm_cfg, frequency)
    for dio in dios:
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(tpwm_cfg["period"], duty), tpwm_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.constraint(valid=several_channels)
def test_channel_duties(fw, ad3, need, tpwm_cfg, timer):
    """Each channel keeps its own duty: the channel number is the position in `pins`."""
    dios = wired(need, timer["pins"])
    timclk = open_tpwm(fw, tpwm_cfg, timer)
    duties = tpwm_cfg["channel_duties"]
    for channel in range(1, len(dios) + 1):
        fw.tpwm.duty(timer["timer"], channel, duties[channel - 1])
    fw.tpwm.start(timer["timer"])
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    capture = record(ad3, tpwm_cfg, frequency)
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(tpwm_cfg["period"], duties[channel - 1]), tpwm_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer_pwm.timers")
def test_pulse_sets_the_period_of_every_channel(fw, ad3, need, tpwm_cfg, timer):
    """`SetPulse(on, period)` writes the channel's compare value and the shared auto-reload: every channel changes
    frequency, the others keep their compare value."""
    dios = wired(need, timer["pins"])
    index, pulse = timer["timer"], tpwm_cfg["pulse"]
    timclk = open_tpwm(fw, tpwm_cfg, timer)
    for channel in range(1, len(dios) + 1):
        fw.tpwm.duty(index, channel, 50)
    fw.tpwm.start(index)
    fw.tpwm.pulse(index, 1, pulse["ccr"], pulse["period"])
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], pulse["period"])
    capture = record(ad3, tpwm_cfg, frequency)
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            ccr = pulse["ccr"] if channel == 1 else tpwm_cfg["period"] * 50 // 100
            check_channel(capture, dio, frequency, ccr / (pulse["period"] + 1), tpwm_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.constraint(valid=several_channels)
def test_unused_channel(fw, ad3, need, tpwm_cfg, timer):
    """A `-` channel gets a `DummyPinStm`: its pin stays free for other groups, the other channels run unchanged."""
    first = next(position for position, pin in enumerate(timer["pins"]) if pin is not None)
    pins = list(timer["pins"])
    free = pins[first]
    pins[first] = None
    dios = wired(need, pins)
    timclk = open_tpwm(fw, tpwm_cfg, timer, pins)
    for channel in range(1, len(pins) + 1):
        fw.tpwm.duty(timer["timer"], channel, 50)
    fw.tpwm.start(timer["timer"])
    fw.gpio.cfg(free, "in")
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    capture = record(ad3, tpwm_cfg, frequency)
    for dio in dios:
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(tpwm_cfg["period"], 50), tpwm_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer_pwm.timers")
@pytest.mark.constraint(valid=several_channels)
def test_start_and_stop_one_channel(fw, ad3, need, tpwm_cfg, timer):
    """`ch=` starts or stops one channel; without it every channel; repeating either is harmless."""
    positions = [position for position, pin in enumerate(timer["pins"]) if pin is not None][:2]
    dios = wired(need, timer["pins"])
    first, second = (dios[position] for position in positions)
    channel = positions[0] + 1
    index = timer["timer"]
    timclk = open_tpwm(fw, tpwm_cfg, timer)
    for number in range(1, len(dios) + 1):
        fw.tpwm.duty(index, number, 50)
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    fraction = pwm_duty_fraction(tpwm_cfg["period"], 50)
    fw.tpwm.start(index, ch=channel)
    fw.tpwm.start(index, ch=channel)
    capture = record(ad3, tpwm_cfg, frequency)
    check_channel(capture, first, frequency, fraction, tpwm_cfg)
    assert is_stopped(capture, second, tpwm_cfg), "a channel that was not started runs"
    fw.tpwm.start(index)
    fw.tpwm.stop(index, ch=channel)
    capture = record(ad3, tpwm_cfg, frequency)
    assert is_stopped(capture, first, tpwm_cfg), "the stopped channel runs"
    check_channel(capture, second, frequency, fraction, tpwm_cfg)
    fw.tpwm.stop(index)
    fw.tpwm.stop(index)
    capture = record(ad3, tpwm_cfg, frequency)
    assert is_stopped(capture, first, tpwm_cfg) and is_stopped(capture, second, tpwm_cfg)
    fw.tpwm.start(index)
    check_channel(record(ad3, tpwm_cfg, frequency), second, frequency, fraction, tpwm_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "timer_pwm.timers")
def test_break_left_by_pwm_is_disabled(fw, ad3, need, board_cfg, tpwm_cfg, timer):
    """`pwm.close` leaves BDTR with the break input enabled; with `brkpol=low` and the break pin back in analog mode
    that break stays active, so `tpwm` on the same timer only drives its outputs because it rewrites BDTR."""
    index = timer["timer"]
    pwm_timer = next((entry for entry in board_cfg.param("pwm.timers") if entry["timer"] == index and entry.get("brk")), None)
    if pwm_timer is None:
        pytest.skip(f"no pwm break input on TIM{index}")
    dios = wired(need, timer["pins"])
    fw.pwm.open(index, pins=[pwm_timer["channels"][0]["pin"]], brk=pwm_timer["brk"], brkpol="low")
    fw.pwm.close(index)
    timclk = open_tpwm(fw, tpwm_cfg, timer)
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            fw.tpwm.duty(index, channel, 50)
    fw.tpwm.start(index)
    frequency = timer_update_rate(timclk, tpwm_cfg["prescaler"], tpwm_cfg["period"])
    capture = record(ad3, tpwm_cfg, frequency)
    for dio in dios:
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(tpwm_cfg["period"], 50), tpwm_cfg)


def test_open_errors(fw, tpwm_cfg, board_cfg):
    """Argument errors in the protocol order: usage, range, pin, unsupported."""
    timer = next(entry for entry in tpwm_cfg["timers"] if entry["timer"] in expect.SINGLE_CHANNEL_TIMERS)
    index, pin = timer["timer"], timer["pins"][0]
    wide = max(tpwm_cfg["timers"], key=lambda entry: len(entry["pins"]))
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
    raw = {f"tpwm.open {index}": "usage", f"tpwm.open {index} pins=,": "usage", f"tpwm.open {index} pins=nosuchalias": "pin"}
    for line, reason in raw.items():
        assert fw.terminal.command(line, check=False).reason == reason, line
    if len(wide["pins"]) > 1:
        reversed_pins = list(reversed(wide["pins"]))
        if reversed_pins != wide["pins"] and reversed_pins[0] is not None:
            expect_error("pin", fw.tpwm.open, wide["timer"], pins=reversed_pins)
    for command, args in (("duty", (1, 50)), ("pulse", (1, 1, 2)), ("start", ()), ("stop", ()), ("close", ())):
        expect_error("notopen", getattr(fw.tpwm, command), index, *args)


def test_channel_errors(fw, need, tpwm_cfg):
    timer = max(tpwm_cfg["timers"], key=lambda entry: len(entry["pins"]))
    need.unloaded(*(pin for pin in timer["pins"] if pin is not None))
    index, channels = timer["timer"], len(timer["pins"])
    maximum = timer_period_max(index)
    fw.tpwm.open(index, pins=timer["pins"])
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
    expect_error("range", fw.tpwm.start, index, ch=channels + 1)
    expect_error("range", fw.tpwm.stop, index, ch=0)
    for line in (f"tpwm.duty {index} 1", f"tpwm.start {index} ch=x", f"tpwm.stop {index} channel=1", f"tpwm.duty {index} 1 50 1"):
        assert fw.terminal.command(line, check=False).reason == "usage", line
    fw.tpwm.duty(index, channels, 100)
    fw.tpwm.pulse(index, channels, maximum, maximum)


def test_timer_shared_with_pwm_and_tim(fw, need, tpwm_cfg):
    """`tpwm` holds its timer against pwm and tim, and one timer at a time."""
    first, second = tpwm_cfg["timers"][:2]
    need.unloaded(*(pin for pin in first["pins"] if pin is not None))
    fw.tpwm.open(first["timer"], pins=first["pins"])
    expect_error("busy", fw.pwm.open, first["timer"], channels=[1])
    expect_error("busy", fw.tim.open, first["timer"], irq="none")
    expect_error("busy", fw.tpwm.open, second["timer"], pins=second["pins"])
    fw.tpwm.close(first["timer"])
    fw.tim.open(first["timer"], irq="none")
    expect_error("busy", fw.tpwm.open, first["timer"], pins=first["pins"])


def test_channel_pins_are_claimed(fw, need, tpwm_cfg):
    """The channel pins belong to `tpwm` while it is open and return to the pool with `tpwm.close`."""
    timer = tpwm_cfg["timers"][0]
    need.unloaded(*(pin for pin in timer["pins"] if pin is not None))
    pin = next(pin for pin in timer["pins"] if pin is not None)
    fw.tpwm.open(timer["timer"], pins=timer["pins"])
    expect_error("busy", fw.gpio.cfg, pin, "in")
    fw.tpwm.close(timer["timer"])
    fw.gpio.cfg(pin, "in")
    expect_error("busy", fw.tpwm.open, timer["timer"], pins=timer["pins"])
