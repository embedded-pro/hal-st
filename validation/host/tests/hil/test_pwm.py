"""PWM (`hal::PwmStm` / `SynchronousPwmStm`): waveforms, channels, complementary outputs with dead time, idle
levels, the break input, frequency changes and the argument checks.

Wiring set `bundle1`: the outputs of `tests.pwm.timers` (channel pins, complementary `npin`s and `brk` inputs) on
DIOs. Frequencies and duties are compared with what the protocol asks for after quantisation to whole counter
ticks (`expect.pwm_frequency`/`pwm_duty`).

Scenarios: features/pwm.feature.
"""

from __future__ import annotations

import bisect
import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect


@pytest.fixture
def pwm_cfg(board_cfg):
    return board_cfg.param("pwm")


@pytest.fixture(scope="module")
def timer_clock(board_cfg):
    return board_cfg.clock("timer")


@pytest.fixture
def state():
    """What the steps of one scenario hand on to the later ones."""
    return {}


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


def expected_waveform(pwmclk, frequency, mode, duty):
    return (
        expect.pwm_frequency(pwmclk, frequency, mode),
        expect.pwm_duty(pwmclk, frequency, mode, duty),
        expect.pwm_duty_step(pwmclk, frequency, mode),
    )


def logical(capture, dio, inverted):
    bits = capture.channel(dio)
    return [1 - bit for bit in bits] if inverted else bits


def switch_overs(off_bits, on_bits, rate):
    """From each turn-off of one output to the next turn-on of the other. Without dead time both switch on the same
    kernel clock edge, so no sample has both off and `analysis.dead_times_split` finds no switch-over at all."""
    ons = analysis.rising_times(on_bits, rate)
    times = []
    for off in analysis.falling_times(off_bits, rate):
        index = bisect.bisect_left(ons, off)
        if index < len(ons):
            times.append(ons[index] - off)
    return times


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.waveform")
@pytest.mark.constraint(valid=supported)
@scenario("pwm.feature", "One channel runs at the set frequency and duty")
def test_waveform(timer, freq, duty, mode, prescaler, sync):
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.limits")
@pytest.mark.constraint(valid=supported)
@scenario("pwm.feature", "The frequency limits of the counter open and change, one step beyond is refused")
def test_frequency_limits(timer, mode, prescaler):
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.channels")
@pytest.mark.constraint(valid=supported)
@scenario("pwm.feature", "The channels run with their own duties and common edges or centres")
def test_channels(timer, count, mode, sync):
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.complementary")
@pytest.mark.constraint(valid=complementary_timer)
@scenario("pwm.feature", "The channel and the complementary output switch over after the dead time")
def test_complementary(timer, dead, inversion, mode, sync):
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.constraint(valid=complementary_timer)
@scenario("pwm.feature", "The complementary output runs alone while the channel pin stays a GPIO")
def test_complementary_only(timer):
    pass


@pytest.mark.board_params("dead", "pwm.dead_limit_ns")
@scenario("pwm.feature", "Dead times beyond 1 ms are refused")
def test_dead_time_limits(dead):
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.idle")
@pytest.mark.constraint(valid=complementary_timer)
@scenario("pwm.feature", "The outputs rest at the idle levels while disabled")
def test_idle_levels(timer, idle, idlen):
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.break")
@pytest.mark.constraint(valid=break_timer)
@scenario("pwm.feature", "The break input disables the outputs")
def test_break_input(timer, brkpol, brkauto):
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@pytest.mark.matrix("pwm.frequency_change")
@pytest.mark.constraint(valid=supported)
@scenario("pwm.feature", "The channel follows frequency changes and stops with the timer")
def test_frequency_change_and_stop(timer, mode, sync):
    pass


@scenario("pwm.feature", "Invalid opens and commands on a closed timer are refused")
def test_open_errors():
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@scenario("pwm.feature", "What the timer lacks is refused")
def test_feature_support(timer):
    pass


@scenario("pwm.feature", "Malformed duties are refused")
def test_duty_errors():
    pass


@scenario("pwm.feature", "One timer runs at a time")
def test_one_timer_at_a_time():
    pass


@scenario("pwm.feature", "A reopen forgets the dead time, the break input and the idle levels")
def test_reopen_forgets_dead_time_and_break():
    pass


@given("the first channel of the timer is wired")
def first_channel_wired(need, state, timer):
    channel = timer["channels"][0]
    state.update(timer=timer, channel=channel, dio=need.dio(channel["pin"]))


@given("as many channels of the timer as the channel count are wired")
def channels_wired(need, state, timer, count):
    selected = timer["channels"][:count]
    state.update(timer=timer, selected=selected, dios=[need.dio(channel["pin"]) for channel in selected])


@given("the first channel of the timer and its complementary output are wired")
def complementary_wired(need, state, timer):
    channel = timer["channels"][0]
    a, b = need.dio(channel["pin"]), need.dio(channel["npin"])
    state.update(timer=timer, channel=channel, a=a, b=b)


@given("the first channel of the timer and its break input are wired")
def break_wired(need, state, timer):
    channel = timer["channels"][0]
    dio, brk = need.dio(channel["pin"]), need.dio(timer["brk"])
    state.update(timer=timer, channel=channel, dio=dio, brk=brk)


@given("its first channel, the complementary output and the break input are wired")
def complementary_and_break_wired(need, state):
    timer = state["timer"]
    channel = timer["channels"][0]
    a, b, brk = need.dio(channel["pin"]), need.dio(channel["npin"]), need.dio(timer["brk"])
    state.update(channel=channel, a=a, b=b, brk=brk)


@given(
    "the duty does not round to a static level at the PWM clock of the prescaler, unless it is 0 or 100 % or the period does not fit "
    "the counter"
)
def duty_resolvable(state, timer_clock, timer, freq, duty, mode, prescaler):
    pwmclk = expect.pwm_clock(timer_clock, prescaler)
    fits = expect.pwm_fits(pwmclk, freq, mode, expect.timer_counter_max(timer["timer"]))
    if fits and duty not in (0, 100) and not expect.pwm_duty_resolvable(pwmclk, freq, mode, duty):
        pytest.skip(f"{duty} % rounds to a static level at {freq} Hz with pwmclk {pwmclk} Hz ({mode})")
    state.update(pwmclk=pwmclk, fits=fits)


@given("the lowest and highest frequency that fit the counter of the timer at the PWM clock of the prescaler in the mode")
def frequency_limits(state, timer_clock, timer, mode, prescaler):
    number, pin = timer["timer"], timer["channels"][0]["pin"]
    pwmclk = expect.pwm_clock(timer_clock, prescaler)
    low, high = expect.pwm_frequency_limits(pwmclk, mode, expect.timer_counter_max(number))
    state.update(timer=timer, number=number, pin=pin, pwmclk=pwmclk, low=low, high=high)


@given("the channel pin is configured as a GPIO output")
def channel_pin_gpio(fw, state):
    fw.gpio.cfg(state["channel"]["pin"], "out")


@given("the first timer whose first channel has a complementary output")
def complementary_timer_of_board(pwm_cfg, state):
    state["timer"] = next(timer for timer in pwm_cfg["timers"] if timer["channels"][0].get("npin"))


@given("the first timer with a break input whose first channel has a complementary output")
def break_and_complementary_timer(pwm_cfg, state):
    state["timer"] = next(timer for timer in pwm_cfg["timers"] if timer.get("brk") and timer["channels"][0].get("npin"))


@given("the break input is driven to the inactive level of the break polarity")
def break_inactive(ad3, state, brkpol):
    state["active"] = 1 if brkpol == "high" else 0
    ad3.dio.drive(state["brk"], 1 - state["active"])


@given("the first channel of the first timer")
def first_channel_of_first_timer(pwm_cfg, state):
    timer = pwm_cfg["timers"][0]
    state.update(number=timer["timer"], channel=timer["channels"][0])


@given("the first timer with two channels is opened on its first two channels")
def two_channels_opened(fw, pwm_cfg, state):
    timer = next(timer for timer in pwm_cfg["timers"] if len(timer["channels"]) >= 2)
    state["number"] = timer["timer"]
    fw.pwm.open(state["number"], pins=[channel["pin"] for channel in timer["channels"][:2]])


@given("the first timer is opened on its first channel")
def first_timer_opened(fw, pwm_cfg, state):
    first, second = pwm_cfg["timers"][:2]
    fw.pwm.open(first["timer"], pins=[first["channels"][0]["pin"]])
    state.update(first=first, second=second)


@when(
    parsers.parse(
        'the channel is opened with the frequency, mode, prescaler and sync, which fails with "{reason}" exactly when the period does '
        "not fit the counter"
    )
)
def open_for_waveform(fw, state, timer, freq, mode, prescaler, sync, reason):
    pwmclk, fits = state["pwmclk"], state["fits"]
    try:
        reported = fw.pwm.open(timer["timer"], pins=[state["channel"]["pin"]], freq=freq, mode=mode, prescaler=prescaler, sync=sync)
    except FirmwareError as error:
        assert error.reason == reason and not fits, f"ERR {error.reason} although {freq} Hz fits pwmclk {pwmclk} Hz"
        state["opened"] = False
        return
    assert fits, "the firmware accepted a period outside the counter"
    state.update(opened=True, reported=reported)


@when("the duty is set and the channel is recorded, triggered on it unless the duty is 0 or 100 %, if it opened")
def record_waveform(fw, ad3, pwm_cfg, state, timer, freq, duty):
    if not state["opened"]:
        return
    fw.pwm.duty(timer["timer"], duty)
    state["capture"] = record(ad3, pwm_cfg, freq, None if duty in (0, 100) else state["dio"])


@when("the first channel is opened at the lowest frequency", target_fixture="reported")
def open_at_lowest(fw, state, mode, prescaler):
    return fw.pwm.open(state["number"], pins=[state["pin"]], freq=state["low"], mode=mode, prescaler=prescaler)


@when("the frequency changes to the highest")
def change_to_highest(fw, state):
    fw.pwm.freq(state["number"], state["high"])


@when("the timer closes")
def close_timer(fw, state):
    fw.pwm.close(state["timer"]["timer"])


@when("the channels are opened in reverse order at the channels frequency with the mode and sync")
def open_channels(fw, pwm_cfg, state, timer, mode, sync):
    state["frequency"] = pwm_cfg["channels_frequency"]
    pins = [channel["pin"] for channel in reversed(state["selected"])]
    state["pwmclk"] = fw.pwm.open(timer["timer"], pins=pins, freq=state["frequency"], mode=mode, sync=sync)


@when("the channels get the channel duties, in reverse order like their pins, and are recorded, triggered on the first channel")
def record_channels(fw, ad3, pwm_cfg, state, timer, count):
    state["duties"] = pwm_cfg["channel_duties"][:count]
    fw.pwm.duty(timer["timer"], *reversed(state["duties"]))
    state["capture"] = record(ad3, pwm_cfg, state["frequency"], state["dios"][0])


@when("the channel and its complementary output are opened at the complementary frequency with the mode, dead time, inversion and sync")
def open_complementary(fw, pwm_cfg, state, timer, dead, inversion, mode, sync):
    channel = state["channel"]
    inv, invn = inversion
    state.update(inv=inv, invn=invn, frequency=pwm_cfg["complementary_frequency"])
    state["pwmclk"] = fw.pwm.open(
        timer["timer"],
        pins=[(channel["pin"], channel["npin"])],
        freq=state["frequency"],
        mode=mode,
        dead=dead,
        inv=inv,
        invn=invn,
        sync=sync,
    )


@when(parsers.parse("the duty is set to {percent:d} %"))
def set_duty(fw, state, percent):
    fw.pwm.duty(state["timer"]["timer"], percent)


@when("both outputs are recorded")
def record_both(ad3, pwm_cfg, state):
    state["capture"] = record(ad3, pwm_cfg, state["frequency"])


@when("only the complementary output is opened at the channels frequency")
def open_complementary_only(fw, pwm_cfg, state, timer):
    state["frequency"] = pwm_cfg["channels_frequency"]
    state["pwmclk"] = fw.pwm.open(timer["timer"], pins=[(None, state["channel"]["npin"])], freq=state["frequency"])


@when("the complementary output is recorded, triggered on it")
def record_complementary(ad3, pwm_cfg, state):
    state["capture"] = record(ad3, pwm_cfg, state["frequency"], state["b"])


@when("the channel and its complementary output are opened at the channels frequency with the idle levels")
def open_with_idle_levels(fw, pwm_cfg, state, timer, idle, idlen):
    channel = state["channel"]
    state["frequency"] = pwm_cfg["channels_frequency"]
    fw.pwm.open(timer["timer"], pins=[(channel["pin"], channel["npin"])], freq=state["frequency"], idle=idle, idlen=idlen)


@when("the timer stops")
def stop_timer(fw, state):
    fw.pwm.stop(state["timer"]["timer"])


@when("the channel is opened at the channels frequency with the break input, the break polarity and brkauto")
def open_with_break(fw, pwm_cfg, state, timer, brkpol, brkauto):
    state["frequency"] = pwm_cfg["channels_frequency"]
    fw.pwm.open(timer["timer"], pins=[state["channel"]["pin"]], freq=state["frequency"], brk=timer["brk"], brkpol=brkpol, brkauto=brkauto)


@when("the break input is driven to the active level of the break polarity and the break settle time passes")
def break_active_settled(ad3, pwm_cfg, state):
    ad3.dio.drive(state["brk"], state["active"])
    time.sleep(pwm_cfg["break_settle_s"])


@when("the break input is driven to the inactive level of the break polarity and the break settle time passes")
def break_inactive_settled(ad3, pwm_cfg, state):
    ad3.dio.drive(state["brk"], 1 - state["active"])
    time.sleep(pwm_cfg["break_settle_s"])


@when("the channel is opened at the first of the frequency changes with the mode and sync")
def open_for_frequency_changes(fw, pwm_cfg, state, timer, mode, sync):
    state["changes"] = pwm_cfg["frequency_changes"]
    state["pwmclk"] = fw.pwm.open(timer["timer"], pins=[state["channel"]["pin"]], freq=state["changes"][0], mode=mode, sync=sync)


@when("the break input is driven high")
def break_high(ad3, state):
    ad3.dio.drive(state["brk"], 1)


@when("the break input is driven low")
def break_low(ad3, state):
    ad3.dio.drive(state["brk"], 0)


@when(
    parsers.parse(
        "the channel and its complementary output are opened at the complementary frequency with a dead time of {dead_ns:d} ns, the break "
        "input active {polarity} and both idle levels {level:d}"
    )
)
def open_with_previous_settings(fw, pwm_cfg, state, dead_ns, polarity, level):
    timer, channel = state["timer"], state["channel"]
    state["frequency"] = pwm_cfg["complementary_frequency"]
    pins = [(channel["pin"], channel["npin"])]
    fw.pwm.open(
        timer["timer"], pins=pins, freq=state["frequency"], dead=dead_ns, brk=timer["brk"], brkpol=polarity, idle=level, idlen=level
    )


@when("the channel and its complementary output are opened at the complementary frequency")
def reopen_plain(fw, pwm_cfg, state):
    channel = state["channel"]
    fw.pwm.open(state["timer"]["timer"], pins=[(channel["pin"], channel["npin"])], freq=pwm_cfg["complementary_frequency"])


@then("it reports the PWM clock of the prescaler, if it opened")
def reports_pwm_clock_if_opened(state):
    if not state["opened"]:
        return
    assert state["reported"] == state["pwmclk"]


@then("the channel runs at the quantised frequency and duty, if it opened")
def waveform_matches(pwm_cfg, state, freq, duty, mode):
    if not state["opened"]:
        return
    frequency, quantised, step = expected_waveform(state["pwmclk"], freq, mode, duty)
    check_waveform(state["capture"], state["dio"], frequency, quantised, pwm_cfg, step)


@then(parsers.parse('opening the first channel one below the lowest or one above the highest frequency fails with "{reason}"'))
def open_beyond_limits_refused(fw, state, mode, prescaler, reason):
    for freq in (state["low"] - 1, state["high"] + 1):
        with pytest.raises(FirmwareError) as error:
            fw.pwm.open(state["number"], pins=[state["pin"]], freq=freq, mode=mode, prescaler=prescaler)
        assert error.value.reason == reason, freq


@then("it reports the PWM clock of the prescaler")
def reports_pwm_clock(state, reported):
    assert reported == state["pwmclk"]


@then(parsers.parse('changing the frequency to one below the lowest or one above the highest fails with "{reason}"'))
def change_beyond_limits_refused(fw, state, reason):
    for freq in (state["low"] - 1, state["high"] + 1):
        with pytest.raises(FirmwareError) as error:
            fw.pwm.freq(state["number"], freq)
        assert error.value.reason == reason, freq


@then("every channel runs at the quantised frequency and duty of its channel duty")
def channels_match(pwm_cfg, state, mode):
    for dio, duty in zip(state["dios"], state["duties"]):
        expected, quantised, step = expected_waveform(state["pwmclk"], state["frequency"], mode, duty)
        check_waveform(state["capture"], dio, expected, quantised, pwm_cfg, step)


@then("the channels line up on their pulse centres in center mode and on their rising edges otherwise")
def channels_aligned(pwm_cfg, state, mode):
    check_aligned(state["capture"], state["dios"], "center" if mode == "center" else "rising", tolerance(pwm_cfg, "alignment_s"))


@then("the outputs with the inversion undone are never active together")
def no_shoot_through(state):
    capture = state["capture"]
    bits_a, bits_b = logical(capture, state["a"], state["inv"]), logical(capture, state["b"], state["invn"])
    rate = capture.rate
    assert analysis.overlap_samples(bits_a, bits_b) == 0, "channel and complementary output active together (shoot-through)"
    state.update(bits_a=bits_a, bits_b=bits_b, rate=rate)


@then("each switch-over waits the dead time in timer kernel clocks")
def dead_times_match(pwm_cfg, timer_clock, state, dead):
    rate, bits_a, bits_b = state["rate"], state["bits_a"], state["bits_b"]
    dead_time = expect.pwm_dead_time(dead, timer_clock)
    allowed = tolerance(pwm_cfg, "dead_ticks") / timer_clock + 2 / rate
    for name, times in (
        ("output off -> complementary on", switch_overs(bits_a, bits_b, rate)),
        ("complementary off -> output on", switch_overs(bits_b, bits_a, rate)),
    ):
        assert times, f"no {name} transitions captured"
        assert statistics.median(times) == pytest.approx(dead_time, abs=allowed), name
    state.update(dead_time=dead_time, allowed=allowed)


@then("the high times of both outputs plus two dead times add up to one period")
def period_adds_up(state, mode):
    rate, dead_time, allowed = state["rate"], state["dead_time"], state["allowed"]
    high_a = statistics.median(analysis.high_low_times(state["bits_a"], rate)[0])
    high_b = statistics.median(analysis.high_low_times(state["bits_b"], rate)[0])
    period = 1 / expect.pwm_frequency(state["pwmclk"], state["frequency"], mode)
    assert high_a + high_b + 2 * dead_time == pytest.approx(period, abs=2 * allowed + 4 / rate)


@then("the channel pin stays low without an edge")
def channel_pin_stays_low(state):
    capture, a = state["capture"], state["a"]
    assert capture.edge_count(a) == 0 and capture.channel(a)[0] == 0, "the unused channel pin changed"


@then(
    parsers.parse(
        "the complementary output runs at the quantised edge-aligned frequency with the complement of the quantised {percent:d} % duty"
    )
)
def complementary_matches(pwm_cfg, state, percent):
    pwmclk, frequency = state["pwmclk"], state["frequency"]
    expected, _, step = expected_waveform(pwmclk, frequency, "edge", percent)
    check_waveform(state["capture"], state["b"], expected, 100 - expect.pwm_duty(pwmclk, frequency, "edge", percent), pwm_cfg, step)


@then(
    parsers.parse(
        'opening that channel and its complementary output with the dead time fails with "{reason}" exactly when the dead time does not fit'
    )
)
def dead_time_limit(fw, state, dead, reason):
    channel = state["timer"]["channels"][0]
    try:
        fw.pwm.open(state["timer"]["timer"], pins=[(channel["pin"], channel["npin"])], dead=dead)
    except FirmwareError as error:
        assert error.reason == reason and not expect.pwm_dead_fits(dead), f"dead={dead}: ERR {error.reason}"
        return
    assert expect.pwm_dead_fits(dead), f"dead={dead} accepted"


@then("both outputs switch before the stop")
def both_switch_before_stop(state):
    running, a, b = state["capture"], state["a"], state["b"]
    assert running.edge_count(a) > 0 and running.edge_count(b) > 0, "outputs must run before the stop"


@then("neither output switches after the stop")
def neither_switches_after_stop(state):
    stopped, a, b = state["capture"], state["a"], state["b"]
    assert stopped.edge_count(a) == 0 and stopped.edge_count(b) == 0, "outputs switch after pwm.stop"


@then("the output and the complementary output end at the idle levels, both low where both idle levels are high")
def idle_levels(state, idle, idlen):
    stopped = state["capture"]
    # RM0434, break function: OCx and OCxN are never driven to their active level together, not even by OISx/OISxN,
    # so idle=1 idlen=1 (both active with inv=invn=0) leaves both at their inactive level.
    expected = (0, 0) if idle and idlen else (idle, idlen)
    assert (stopped.channel(state["a"])[-1], stopped.channel(state["b"])[-1]) == expected


@then("the channel switches while the break input is inactive")
def runs_before_break(ad3, pwm_cfg, state):
    assert record(ad3, pwm_cfg, state["frequency"]).edge_count(state["dio"]) > 0, "outputs must run while the break input is inactive"


@then("the channel does not switch during the break")
def off_during_break(ad3, pwm_cfg, state):
    assert record(ad3, pwm_cfg, state["frequency"]).edge_count(state["dio"]) == 0, "outputs keep switching during a break"


@then("the channel switches again exactly when brkauto is set")
def resumes_with_brkauto(ad3, pwm_cfg, state, brkauto):
    resumed = record(ad3, pwm_cfg, state["frequency"]).edge_count(state["dio"])
    assert (resumed > 0) is bool(brkauto), f"{resumed} edges after the break released with brkauto={brkauto}"


@then("the channel switches again after pwm.duty")
def restarts_with_duty(ad3, pwm_cfg, state):
    assert record(ad3, pwm_cfg, state["frequency"]).edge_count(state["dio"]) > 0, "pwm.duty must restart the outputs"


@then(parsers.parse("the channel runs at the quantised frequency and {percent:d} % duty after each of the frequency changes"))
def follows_frequency_changes(fw, ad3, pwm_cfg, state, timer, mode, percent):
    dio = state["dio"]
    for frequency in state["changes"]:
        fw.pwm.freq(timer["timer"], frequency)
        capture = record(ad3, pwm_cfg, frequency, dio)
        expected, quantised, step = expected_waveform(state["pwmclk"], frequency, mode, percent)
        check_waveform(capture, dio, expected, quantised, pwm_cfg, step)


@then("the channel does not toggle at the last of the frequency changes")
def stopped_at_last_frequency(ad3, pwm_cfg, state):
    assert record(ad3, pwm_cfg, state["changes"][-1]).edge_count(state["dio"]) == 0, "the output toggles after pwm.stop"


@then("every invalid open setting fails with its reason")
def open_settings_refused(fw, pwm_cfg, state):
    number, channel = state["number"], state["channel"]
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


@then(parsers.parse('opening the channel pin with two complementary outputs in one entry fails with "{reason}"'))
def two_complementary_outputs_refused(fw, state, reason):
    with pytest.raises(FirmwareError) as error:
        fw.command("pwm.open", state["number"], pins=f"{fw.pin(state['channel']['pin'])}:-:-")
    assert error.value.reason == reason, "one complementary output per entry"


@then(parsers.parse('pwm.duty, pwm.freq, pwm.stop and pwm.close fail with "{reason}"'))
def closed_timer_refused(fw, state, reason):
    for name, args in (("pwm.duty", (50,)), ("pwm.freq", (1000,)), ("pwm.stop", ()), ("pwm.close", ())):
        with pytest.raises(FirmwareError) as error:
            fw.command(name, state["number"], *args)
        assert error.value.reason == reason, name


@then(parsers.parse('every open setting the timer lacks fails with "{reason}"'))
def lacking_features_refused(fw, pwm_cfg, timer, reason):
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
        assert error.value.reason == reason, options


@then(parsers.parse('every malformed duty list fails with "{reason}"'))
def malformed_duties_refused(fw, state, reason):
    for duties in (("10", "20", "30"), ("101",), ("100.5",), ("12.34567",), ("0x10",), (".5",), ("-1",)):
        with pytest.raises(FirmwareError) as error:
            fw.command("pwm.duty", state["number"], *duties)
        assert error.value.reason == reason, duties


@then(parsers.parse('the duties "{first_duty}" and "{second_duty}" are accepted'))
def two_duties_accepted(fw, state, first_duty, second_duty):
    fw.command("pwm.duty", state["number"], first_duty, second_duty)


@then(parsers.parse('the duty "{duty_text}" is accepted'))
def duty_accepted(fw, state, duty_text):
    fw.command("pwm.duty", state["number"], duty_text)


@then(parsers.parse('opening the first or the second timer on its first channel fails with "{reason}"'))
def second_open_refused(fw, state, reason):
    for timer in (state["first"], state["second"]):
        with pytest.raises(FirmwareError) as error:
            fw.pwm.open(timer["timer"], pins=[timer["channels"][0]["pin"]])
        assert error.value.reason == reason, timer["name"]


@then("both outputs switch despite the previous break settings")
def both_switch_after_reopen(state):
    capture, a, b = state["capture"], state["a"], state["b"]
    assert capture.edge_count(a) > 0 and capture.edge_count(b) > 0, "outputs held off by the previous break settings"


@then("the switch-overs wait no dead time")
def no_dead_time(pwm_cfg, timer_clock, state):
    capture = state["capture"]
    bits_a, bits_b = capture.channel(state["a"]), capture.channel(state["b"])
    times = switch_overs(bits_a, bits_b, capture.rate) + switch_overs(bits_b, bits_a, capture.rate)
    assert times, "no switch-overs captured"
    allowed = tolerance(pwm_cfg, "dead_ticks") / timer_clock + 2 / capture.rate
    assert statistics.median(times) == pytest.approx(0, abs=allowed), "dead time of the previous open"
