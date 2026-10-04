"""PWM (`hal::PwmStm` / `SynchronousPwmStm`): waveforms, channels, complementary outputs with dead time, idle
levels, the break input, frequency changes and the argument checks.

Wiring set `bundle1`: the outputs of `tests.pwm.timers` (channel pins, complementary `npin`s and `brk` inputs) on
DIOs. Frequencies and duties are compared with what the protocol asks for after quantisation to whole counter
ticks (`expect.pwm_frequency`/`pwm_duty`); the centre-aligned period of the current driver is a known gap
(`expect.pwm_frequency_driver`), an expected failure of `test_waveform` where it leaves the frequency tolerance.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import expect


@pytest.fixture
def pwm_cfg(board_cfg):
    return board_cfg.param("pwm")


@pytest.fixture(scope="module")
def timer_clock(board_cfg):
    return board_cfg.clock("timer")


def supported(values):
    """Centre alignment needs a timer with a counter mode select; `count` channels must be wired."""
    timer = values.get("timer")
    if timer is None:
        return True
    if values.get("mode") == "center" and not expect.timer_has_center_mode(timer["timer"]):
        return False
    return values.get("count") is None or values["count"] <= len(timer["channels"])


def complementary_timer(values):
    timer = values.get("timer")
    return supported(values) and (timer is None or bool(timer["channels"][0].get("npin")))


def break_timer(values):
    timer = values.get("timer")
    return supported(values) and (timer is None or bool(timer.get("brk")))


def tolerance(pwm_cfg, key):
    return pwm_cfg["tolerance"][key]


def record(ad3, pwm_cfg, frequency, trigger_dio=None, periods=None):
    periods = periods or pwm_cfg["capture_periods"]
    trigger = None if trigger_dio is None else (trigger_dio, "rising")
    return ad3.logic.record_for(periods / frequency, trigger=trigger, timeout=periods / frequency + 2)


def check_waveform(capture, dio, frequency, duty, pwm_cfg, step=0.0):
    """`frequency` and `duty` are the expected (quantised) values; `step` is the duty resolution in percent."""
    if duty in (0, 100):
        bits = capture.channel(dio)
        level = 1 if duty == 100 else 0
        fraction = bits.count(level) / len(bits)
        assert fraction >= tolerance(pwm_cfg, "static_fraction"), f"DIO{dio}: {fraction:.4f} of the samples at {level} for duty {duty}"
        return
    assert capture.frequency(dio) == pytest.approx(frequency, rel=tolerance(pwm_cfg, "frequency")), f"DIO{dio} frequency"
    quantisation = 100 * 2 * frequency / capture.rate + step
    assert capture.duty(dio) * 100 == pytest.approx(duty, abs=tolerance(pwm_cfg, "duty") + quantisation), f"DIO{dio} duty"


def check_aligned(capture, dios, feature, allowed):
    def times(dio):
        bits = capture.channel(dio)
        if feature == "rising":
            return analysis.rising_times(bits, capture.rate)
        return analysis.pulse_centers(bits, capture.rate)

    reference = times(dios[0])[1:-1]
    assert reference, "no complete pulses captured"
    for dio in dios[1:]:
        offsets = analysis.nearest_offsets(reference, times(dio))
        worst = max(abs(offset) for offset in offsets)
        assert worst <= allowed + 2 / capture.rate, f"DIO{dio} {feature} edges off by {worst * 1e9:.0f} ns"


CENTER_PERIOD_GAP = (
    "known gap: hal_st/stm32fxxx/PwmStm.cpp:381-390 - centre aligned the period is 2 x (ticks/2 - 1) counter ticks "
    "instead of ticks (expect.pwm_frequency_driver)"
)


def expect_center_period_gap(request, pwm_cfg, pwmclk, frequency, mode):
    """The centre-aligned period gap fails the frequency check only where the driver's period leaves the tolerance
    (short periods; the duty error stays within one step there); `--fake` ignores it like the board files' gaps."""
    if mode != "center" or request.config.getoption("--fake"):
        return
    error = abs(expect.pwm_frequency_driver(pwmclk, frequency, mode) / expect.pwm_frequency(pwmclk, frequency, mode) - 1)
    if error > tolerance(pwm_cfg, "frequency"):
        request.applymarker(pytest.mark.xfail(strict=False, reason=CENTER_PERIOD_GAP))


def expected_waveform(pwmclk, frequency, mode, duty):
    return (
        expect.pwm_frequency(pwmclk, frequency, mode),
        expect.pwm_duty(pwmclk, frequency, mode, duty),
        expect.pwm_duty_step(pwmclk, frequency, mode),
    )


@pytest.mark.ad3
@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.waveform")
@pytest.mark.constraint(valid=supported)
def test_waveform(request, fw, ad3, need, pwm_cfg, timer_clock, timer, freq, duty, mode, prescaler, sync):
    """One channel: `ERR range` exactly where the period does not fit the counter, else frequency and duty."""
    channel = timer["channels"][0]
    dio = need.dio(channel["pin"])
    pwmclk = expect.pwm_clock(timer_clock, prescaler)
    fits = expect.pwm_fits(pwmclk, freq, mode, expect.timer_counter_max(timer["timer"]))
    if fits and duty not in (0, 100) and not expect.pwm_duty_resolvable(pwmclk, freq, mode, duty):
        pytest.skip(f"{duty} % rounds to a static level at {freq} Hz with pwmclk {pwmclk} Hz ({mode})")
    try:
        reported = fw.pwm.open(timer["timer"], pins=[channel["pin"]], freq=freq, mode=mode, prescaler=prescaler, sync=sync)
    except FirmwareError as error:
        assert error.reason == "range" and not fits, f"ERR {error.reason} although {freq} Hz fits pwmclk {pwmclk} Hz"
        return
    assert fits, "the firmware accepted a period outside the counter"
    assert reported == pwmclk
    if duty not in (0, 100):
        expect_center_period_gap(request, pwm_cfg, pwmclk, freq, mode)
    fw.pwm.duty(timer["timer"], duty)
    capture = record(ad3, pwm_cfg, freq, None if duty in (0, 100) else dio)
    frequency, quantised, step = expected_waveform(pwmclk, freq, mode, duty)
    check_waveform(capture, dio, frequency, quantised, pwm_cfg, step)


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.limits")
@pytest.mark.constraint(valid=supported)
def test_frequency_limits(fw, timer_clock, timer, mode, prescaler):
    """The lowest and highest frequency that fit open and change; one step beyond is `ERR range`, in `pwm.open`
    and in `pwm.freq` (under 2 counter ticks, or beyond 16 bits, 32 bits on TIM2, half the period centre aligned)."""
    number, pin = timer["timer"], timer["channels"][0]["pin"]
    pwmclk = expect.pwm_clock(timer_clock, prescaler)
    low, high = expect.pwm_frequency_limits(pwmclk, mode, expect.timer_counter_max(number))
    for freq in (low - 1, high + 1):
        with pytest.raises(FirmwareError) as error:
            fw.pwm.open(number, pins=[pin], freq=freq, mode=mode, prescaler=prescaler)
        assert error.value.reason == "range", freq
    assert fw.pwm.open(number, pins=[pin], freq=low, mode=mode, prescaler=prescaler) == pwmclk
    fw.pwm.freq(number, high)
    for freq in (low - 1, high + 1):
        with pytest.raises(FirmwareError) as error:
            fw.pwm.freq(number, freq)
        assert error.value.reason == "range", freq
    fw.pwm.close(number)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.channels")
@pytest.mark.constraint(valid=supported)
def test_channels(fw, ad3, need, pwm_cfg, timer, count, mode, sync):
    """1-4 channels of one timer with their own duties (in the order of the command) and common edges or centres."""
    selected = timer["channels"][:count]
    dios = [need.dio(channel["pin"]) for channel in selected]
    frequency = pwm_cfg["channels_frequency"]
    duties = pwm_cfg["channel_duties"][:count]
    pins = [channel["pin"] for channel in reversed(selected)]
    pwmclk = fw.pwm.open(timer["timer"], pins=pins, freq=frequency, mode=mode, sync=sync)
    fw.pwm.duty(timer["timer"], *reversed(duties))
    capture = record(ad3, pwm_cfg, frequency, dios[0])
    for dio, duty in zip(dios, duties):
        expected, quantised, step = expected_waveform(pwmclk, frequency, mode, duty)
        check_waveform(capture, dio, expected, quantised, pwm_cfg, step)
    check_aligned(capture, dios, "center" if mode == "center" else "rising", tolerance(pwm_cfg, "alignment_s"))


def logical(capture, dio, inverted):
    bits = capture.channel(dio)
    return [1 - bit for bit in bits] if inverted else bits


@pytest.mark.ad3
@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.complementary")
@pytest.mark.constraint(valid=complementary_timer)
def test_complementary(fw, ad3, need, pwm_cfg, timer_clock, timer, dead, inversion, mode, sync):
    """Channel and complementary output never active together; each switch-over waits the dead time (counted in
    timer kernel clocks, saturating at the largest DTG value); `inv`/`invn` are undone before comparing."""
    channel = timer["channels"][0]
    a, b = need.dio(channel["pin"]), need.dio(channel["npin"])
    inv, invn = inversion
    frequency = pwm_cfg["complementary_frequency"]
    pwmclk = fw.pwm.open(
        timer["timer"], pins=[(channel["pin"], channel["npin"])], freq=frequency, mode=mode, dead=dead, inv=inv, invn=invn, sync=sync
    )
    fw.pwm.duty(timer["timer"], 40)
    capture = record(ad3, pwm_cfg, frequency)
    bits_a, bits_b = logical(capture, a, inv), logical(capture, b, invn)
    rate = capture.rate
    assert analysis.overlap_samples(bits_a, bits_b) == 0, "channel and complementary output active together (shoot-through)"
    split = analysis.dead_times_split(bits_a, bits_b, rate)
    dead_time = expect.pwm_dead_time(dead, timer_clock)
    allowed = tolerance(pwm_cfg, "dead_ticks") / timer_clock + 2 / rate
    for name, times in (("output off -> complementary on", split.a_off_to_b_on), ("complementary off -> output on", split.b_off_to_a_on)):
        assert times, f"no {name} transitions captured"
        assert statistics.median(times) == pytest.approx(dead_time, abs=allowed), name
    high_a = statistics.median(analysis.high_low_times(bits_a, rate)[0])
    high_b = statistics.median(analysis.high_low_times(bits_b, rate)[0])
    period = 1 / expect.pwm_frequency(pwmclk, frequency, mode)
    assert high_a + high_b + 2 * dead_time == pytest.approx(period, abs=2 * allowed + 4 / rate)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.constraint(valid=complementary_timer)
def test_complementary_only(fw, ad3, need, pwm_cfg, timer):
    """`-:<npin>` drives the complementary output only (the complement of the channel's reference, as the driver
    enables both outputs); the channel pin stays a GPIO driven low."""
    channel = timer["channels"][0]
    a, b = need.dio(channel["pin"]), need.dio(channel["npin"])
    fw.gpio.cfg(channel["pin"], "out")
    frequency = pwm_cfg["channels_frequency"]
    pwmclk = fw.pwm.open(timer["timer"], pins=[(None, channel["npin"])], freq=frequency)
    fw.pwm.duty(timer["timer"], 30)
    capture = record(ad3, pwm_cfg, frequency, b)
    assert capture.edge_count(a) == 0 and capture.channel(a)[0] == 0, "the unused channel pin changed"
    expected, _, step = expected_waveform(pwmclk, frequency, "edge", 30)
    check_waveform(capture, b, expected, 100 - expect.pwm_duty(pwmclk, frequency, "edge", 30), pwm_cfg, step)


@pytest.mark.board_params("dead", "pwm.dead_limit_ns")
def test_dead_time_limits(fw, pwm_cfg, dead):
    """`dead` up to 1 ms opens (saturating at the largest DTG value); beyond it `ERR range`."""
    timer = next(timer for timer in pwm_cfg["timers"] if timer["channels"][0].get("npin"))
    channel = timer["channels"][0]
    try:
        fw.pwm.open(timer["timer"], pins=[(channel["pin"], channel["npin"])], dead=dead)
    except FirmwareError as error:
        assert error.reason == "range" and not expect.pwm_dead_fits(dead), f"dead={dead}: ERR {error.reason}"
        return
    assert expect.pwm_dead_fits(dead), f"dead={dead} accepted"


@pytest.mark.ad3
@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.idle")
@pytest.mark.constraint(valid=complementary_timer)
def test_idle_levels(fw, ad3, need, pwm_cfg, timer, idle, idlen):
    """`idle`/`idlen` are the levels of the output and the complementary output while the outputs are disabled."""
    channel = timer["channels"][0]
    a, b = need.dio(channel["pin"]), need.dio(channel["npin"])
    frequency = pwm_cfg["channels_frequency"]
    fw.pwm.open(timer["timer"], pins=[(channel["pin"], channel["npin"])], freq=frequency, idle=idle, idlen=idlen)
    fw.pwm.duty(timer["timer"], 50)
    running = record(ad3, pwm_cfg, frequency)
    assert running.edge_count(a) > 0 and running.edge_count(b) > 0, "outputs must run before the stop"
    fw.pwm.stop(timer["timer"])
    stopped = record(ad3, pwm_cfg, frequency)
    assert stopped.edge_count(a) == 0 and stopped.edge_count(b) == 0, "outputs switch after pwm.stop"
    assert (stopped.channel(a)[-1], stopped.channel(b)[-1]) == (idle, idlen)


@pytest.mark.ad3
@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.break")
@pytest.mark.constraint(valid=break_timer)
def test_break_input(fw, ad3, need, pwm_cfg, timer, brkpol, brkauto):
    """The break input at its active level (`brkpol`) disables the outputs; with `brkauto=1` they come back when
    it releases, otherwise they stay off until the next `pwm.duty`."""
    channel = timer["channels"][0]
    dio, brk = need.dio(channel["pin"]), need.dio(timer["brk"])
    active = 1 if brkpol == "high" else 0
    frequency = pwm_cfg["channels_frequency"]
    ad3.dio.drive(brk, 1 - active)
    fw.pwm.open(timer["timer"], pins=[channel["pin"]], freq=frequency, brk=timer["brk"], brkpol=brkpol, brkauto=brkauto)
    fw.pwm.duty(timer["timer"], 50)
    assert record(ad3, pwm_cfg, frequency).edge_count(dio) > 0, "outputs must run while the break input is inactive"
    ad3.dio.drive(brk, active)
    time.sleep(pwm_cfg["break_settle_s"])
    assert record(ad3, pwm_cfg, frequency).edge_count(dio) == 0, "outputs keep switching during a break"
    ad3.dio.drive(brk, 1 - active)
    time.sleep(pwm_cfg["break_settle_s"])
    resumed = record(ad3, pwm_cfg, frequency).edge_count(dio)
    assert (resumed > 0) is bool(brkauto), f"{resumed} edges after the break released with brkauto={brkauto}"
    fw.pwm.duty(timer["timer"], 50)
    assert record(ad3, pwm_cfg, frequency).edge_count(dio) > 0, "pwm.duty must restart the outputs"


@pytest.mark.ad3
@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.frequency_change")
@pytest.mark.constraint(valid=supported)
def test_frequency_change_and_stop(fw, ad3, need, pwm_cfg, timer, mode, sync):
    channel = timer["channels"][0]
    dio = need.dio(channel["pin"])
    changes = pwm_cfg["frequency_changes"]
    pwmclk = fw.pwm.open(timer["timer"], pins=[channel["pin"]], freq=changes[0], mode=mode, sync=sync)
    fw.pwm.duty(timer["timer"], 50)
    for frequency in changes:
        fw.pwm.freq(timer["timer"], frequency)
        capture = record(ad3, pwm_cfg, frequency, dio)
        expected, quantised, step = expected_waveform(pwmclk, frequency, mode, 50)
        check_waveform(capture, dio, expected, quantised, pwm_cfg, step)
    fw.pwm.stop(timer["timer"])
    assert record(ad3, pwm_cfg, changes[-1]).edge_count(dio) == 0, "the output toggles after pwm.stop"


def test_open_errors(fw, pwm_cfg):
    timer = pwm_cfg["timers"][0]
    number, channel = timer["timer"], timer["channels"][0]
    foreign = pwm_cfg["foreign_pin"]
    cases = [
        ({}, "usage"),
        ({"channels": [5]}, "range"),
        ({"channels": [0]}, "range"),
        ({"channels": [1, 1]}, "usage"),
        ({"channels": [1, 2, 3, 4, 1]}, "usage"),
        ({"channels": [1, 2], "pins": [channel["pin"]]}, "usage"),
        ({"pins": [(None, None)]}, "usage"),
        ({"pins": [foreign]}, "pin"),
        ({"pins": [channel["pin"], channel["pin"]]}, "usage"),
        ({"channels": [channel["channel"]], "mode": "sideways"}, "usage"),
        ({"channels": [channel["channel"]], "prescaler": expect.PWM_PRESCALER_MAX + 1}, "range"),
        ({"channels": [channel["channel"]], "freq": 0}, "range"),
        ({"channels": [channel["channel"]], "dead": "fast"}, "usage"),
        ({"channels": [channel["channel"]], "dead": expect.PWM_DEAD_MAX_NS + 1}, "range"),
        ({"channels": [channel["channel"]], "inv": 2}, "range"),
        ({"channels": [channel["channel"]], "brkpol": "sideways"}, "usage"),
        ({"channels": [channel["channel"]], "brk": foreign}, "pin"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.pwm.open(number, **options)
        assert error.value.reason == reason, options
    with pytest.raises(FirmwareError) as error:
        fw.command("pwm.open", number, pins=f"{fw.pin(channel['pin'])}:-:-")
    assert error.value.reason == "usage", "one complementary output per entry"
    for name, args in (("pwm.duty", (50,)), ("pwm.freq", (1000,)), ("pwm.stop", ()), ("pwm.close", ())):
        with pytest.raises(FirmwareError) as error:
            fw.command(name, number, *args)
        assert error.value.reason == "notopen", name


@pytest.mark.board_params("timer", "pwm.timers")
def test_feature_support(fw, pwm_cfg, timer):
    """What the timer lacks is `ERR unsupported`: centre alignment on TIM16/TIM17, channels beyond the timer's,
    CH4N, and complementary outputs, dead time, idle levels and the break input on timers without a break function."""
    number, channel = timer["timer"], timer["channels"][0]
    foreign = pwm_cfg["foreign_pin"]
    cases = []
    if not expect.timer_has_center_mode(number):
        cases.append({"channels": [channel["channel"]], "mode": "center"})
    if not expect.timer_has_channel(number, 2):
        cases.append({"channels": [2]})
    if expect.timer_has_break(number) and expect.timer_has_channel(number, 4):
        cases.append({"channels": [4], "pins": [(None, channel.get("npin") or foreign)]})
    if not expect.timer_has_break(number):
        cases += [
            {"pins": [channel["pin"]], "dead": 0},
            {"pins": [channel["pin"]], "idle": 1},
            {"pins": [channel["pin"]], "idlen": 1},
            {"pins": [channel["pin"]], "brk": foreign},
            {"pins": [(channel["pin"], foreign)]},
        ]
    for options in cases:
        with pytest.raises(FirmwareError) as error:
            fw.pwm.open(number, **options)
        assert error.value.reason == "unsupported", options


def test_duty_errors(fw, pwm_cfg):
    timer = next(timer for timer in pwm_cfg["timers"] if len(timer["channels"]) >= 2)
    number = timer["timer"]
    fw.pwm.open(number, pins=[channel["pin"] for channel in timer["channels"][:2]])
    for duties in (("10", "20", "30"), ("101",), ("100.5",), ("12.34567",), ("0x10",), (".5",), ("-1",)):
        with pytest.raises(FirmwareError) as error:
            fw.command("pwm.duty", number, *duties)
        assert error.value.reason == "usage", duties
    fw.command("pwm.duty", number, "12.5", "100")
    fw.command("pwm.duty", number, "0.0001")


def test_one_timer_at_a_time(fw, pwm_cfg):
    first, second = pwm_cfg["timers"][:2]
    fw.pwm.open(first["timer"], pins=[first["channels"][0]["pin"]])
    for timer in (first, second):
        with pytest.raises(FirmwareError) as error:
            fw.pwm.open(timer["timer"], pins=[timer["channels"][0]["pin"]])
        assert error.value.reason == "busy", timer["name"]


@pytest.mark.ad3
def test_reopen_forgets_dead_time_and_break(fw, ad3, need, pwm_cfg, timer_clock):
    """Each open rebuilds the timer: a previous open's dead time, break input and idle levels do not survive."""
    timer = next(timer for timer in pwm_cfg["timers"] if timer.get("brk") and timer["channels"][0].get("npin"))
    channel = timer["channels"][0]
    a, b, brk = need.dio(channel["pin"]), need.dio(channel["npin"]), need.dio(timer["brk"])
    frequency = pwm_cfg["complementary_frequency"]
    pins = [(channel["pin"], channel["npin"])]
    ad3.dio.drive(brk, 1)
    fw.pwm.open(timer["timer"], pins=pins, freq=frequency, dead=2000, brk=timer["brk"], brkpol="low", idle=1, idlen=1)
    fw.pwm.duty(timer["timer"], 40)
    fw.pwm.close(timer["timer"])
    ad3.dio.drive(brk, 0)
    fw.pwm.open(timer["timer"], pins=pins, freq=frequency)
    fw.pwm.duty(timer["timer"], 40)
    capture = record(ad3, pwm_cfg, frequency)
    assert capture.edge_count(a) > 0 and capture.edge_count(b) > 0, "outputs held off by the previous break settings"
    split = analysis.dead_times_split(capture.channel(a), capture.channel(b), capture.rate)
    times = split.a_off_to_b_on + split.b_off_to_a_on
    assert times, "no switch-overs captured"
    allowed = tolerance(pwm_cfg, "dead_ticks") / timer_clock + 2 / capture.rate
    assert statistics.median(times) == pytest.approx(0, abs=allowed), "dead time of the previous open"
