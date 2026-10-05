"""PWM extensions (`hal::PwmStm`, PROTOCOL.md D.10): the five counter modes, compare preload, the break input
filter, and the trigger output an `adc.open trgo=` sequence runs on.

Wiring set `bundle1`: the outputs and break inputs of `tests.pwm.timers` on DIOs (`tests.pwm_ext` names the
entries); the TRGO and argument tests need no AD3.

Scenarios: features/pwm_ext.feature.
"""

from __future__ import annotations

import math
import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect

# The waveform each mode gives: every centre-aligned mode counts up and down; `edgedown` counts down.
ALIGNMENTS: dict[str, expect.PwmMode] = {
    "edge": "edge",
    "edgedown": "edge",
    "center": "center",
    "centerup": "center",
    "centerboth": "center",
}
# Where the outputs of one timer line up: the period starts counting up (rising edges) and ends counting down
# (falling edges); centre-aligned pulses share their centres.
FEATURES = {"edge": "rising", "edgedown": "falling"}
# BDTR.BKF: (fDTS divider, consecutive samples); the filter needs divider x samples kernel clocks (CKD = 1).
BREAK_FILTERS = {
    0: (1, 0),
    1: (1, 2),
    2: (1, 4),
    3: (1, 8),
    4: (2, 6),
    5: (2, 8),
    6: (4, 6),
    7: (4, 8),
    8: (8, 6),
    9: (8, 8),
    10: (16, 5),
    11: (16, 6),
    12: (16, 8),
    13: (32, 5),
    14: (32, 6),
    15: (32, 8),
}


@pytest.fixture
def ext_cfg(board_cfg):
    return board_cfg.param("pwm_ext")


@pytest.fixture(scope="module")
def timer_clock(board_cfg):
    return board_cfg.clock("timer")


@pytest.fixture
def state():
    """What the steps of one scenario hand on to the later ones."""
    return {}


def pwm_timer(board_cfg, name):
    return next(timer for timer in board_cfg.param("pwm.timers") if timer["name"] == name)


def break_filter_ticks(value):
    divider, samples = BREAK_FILTERS[value]
    return divider * samples


def record(ad3, ext_cfg, frequency, trigger_dio=None, periods=None):
    periods = periods or ext_cfg["capture_periods"]
    trigger = None if trigger_dio is None else (trigger_dio, "rising")
    return ad3.logic.record_for(periods / frequency, trigger=trigger, timeout=periods / frequency + 2)


def feature_times(capture, dio, feature):
    bits = capture.channel(dio)
    if feature == "rising":
        return analysis.rising_times(bits, capture.rate)
    if feature == "falling":
        return analysis.falling_times(bits, capture.rate)
    return analysis.pulse_centers(bits, capture.rate)


def timed_measure(fw, adc, runs, repeats, baud):
    """Shortest of `repeats` round trips of `adc.measure n=<runs>`, less the transmission of its reply."""
    shortest = math.inf
    for _ in range(repeats):
        start = time.monotonic()
        samples = fw.adc.measure(adc, n=runs, cmd_timeout=3.0)
        shortest = min(shortest, time.monotonic() - start)
        assert len(samples) == runs
    return shortest - expect.uart_transfer_time(len(",".join(str(sample) for sample in samples)), baud)


@pytest.mark.board_params("mode", "pwm_ext.modes")
@scenario("pwm_ext.feature", "Every mode gives its waveform and lines up the outputs of one timer")
def test_alignment(mode):
    pass


@scenario("pwm_ext.feature", "Counting down, 0 % keeps one counter tick high per period")
def test_edgedown_cannot_reach_zero():
    pass


@pytest.mark.board_params("timer", "pwm.timers")
@scenario("pwm_ext.feature", "The counter modes and trgo need a master timer")
def test_modes_and_trgo_need_a_master_timer(timer):
    pass


@pytest.mark.parametrize("preload", [0, 1])
@scenario("pwm_ext.feature", "Compare preload holds a duty write until the next period")
def test_preload(preload):
    pass


@pytest.mark.board_params("name", "pwm_ext.break_filter_timers")
@pytest.mark.board_params("case", "pwm_ext.break_filter_cases")
@scenario("pwm_ext.feature", "The break filter ignores a break pulse shorter than the filter")
def test_break_filter(name, case):
    pass


@pytest.mark.matrix("pwm_ext.trgo")
@scenario("pwm_ext.feature", "The TRGO of the timer paces the ADC")
def test_trgo_paces_adc(timer, source):
    pass


@scenario("pwm_ext.feature", "Invalid ADC trgo settings are refused")
def test_trgo_errors():
    pass


@scenario("pwm_ext.feature", "Invalid break filter, preload, mode and trgo settings are refused")
def test_open_errors():
    pass


@given("the first two channels of the alignment timer are wired")
def alignment_channels_wired(need, board_cfg, ext_cfg, state):
    timer = pwm_timer(board_cfg, ext_cfg["alignment_timer"])
    channels = timer["channels"][:2]
    state.update(number=timer["timer"], channels=channels, dios=[need.dio(channel["pin"]) for channel in channels])


@given("the first channel of the alignment timer is wired")
def alignment_channel_wired(need, board_cfg, ext_cfg, state):
    timer = pwm_timer(board_cfg, ext_cfg["alignment_timer"])
    pin = timer["channels"][0]["pin"]
    state.update(number=timer["timer"], pin=pin, dio=need.dio(pin))


@given("a prescaler that makes the counter tick last the zero-duty tick")
def zero_duty_prescaler(ext_cfg, timer_clock, state):
    settings = ext_cfg["zero_duty"]
    tick = settings["tick_us"] / 1e6
    state.update(tick=tick, frequency=settings["frequency"], periods=settings["periods"], prescaler=round(timer_clock * tick) - 1)


@given("the first channel of the preload timer is wired")
def preload_channel_wired(need, board_cfg, ext_cfg, state):
    timer = pwm_timer(board_cfg, ext_cfg["preload_timer"])
    number, pin = timer["timer"], timer["channels"][0]["pin"]
    state.update(number=number, pin=pin, dio=need.dio(pin))


@given("the break filter timer has a wired break input")
def break_filter_timer(board_cfg, state, name):
    timer = pwm_timer(board_cfg, name)
    pin, brk = timer["channels"][0]["pin"], timer.get("brk")
    if brk is None:
        pytest.skip(f"{name} has no wired break input")
    state.update(number=timer["timer"], pin=pin, brk=brk)


@given("its first channel and its break input are wired")
def break_filter_wired(need, state):
    dio, brk_dio = need.dio(state["pin"]), need.dio(state["brk"])
    state.update(dio=dio, brk_dio=brk_dio)


@given("the break filter case trips exactly when its pulse outlasts the filter")
def break_filter_case(state, case):
    state["filter_ticks"] = break_filter_ticks(case["filter"])
    assert case["trips"] == (case["ticks"] > state["filter_ticks"]), "tests.pwm_ext.break_filter_cases contradicts BKF"


@given("the first ADC input and the first channel of the timer are unloaded")
def trgo_unloaded(need, board_cfg, state, timer):
    entry = pwm_timer(board_cfg, timer)
    adc_cfg = board_cfg.param("adc")
    adc, pin = adc_cfg["adc"], adc_cfg["inputs"][0]
    need.unloaded(pin, entry["channels"][0]["pin"])
    state.update(entry=entry, number=entry["timer"], adc=adc, adc_pin=pin)


@given("the first ADC input and the first timer of the TRGO matrix")
def trgo_error_setup(board_cfg, ext_cfg, state):
    adc_cfg = board_cfg.param("adc")
    adc, pin = adc_cfg["adc"], adc_cfg["inputs"][0]
    number = pwm_timer(board_cfg, ext_cfg["trgo"]["timer"][0])["timer"]
    state.update(adc=adc, adc_pin=pin, number=number)


@given("the alignment timer and its break input")
def alignment_timer_and_break(board_cfg, ext_cfg, state):
    timer = pwm_timer(board_cfg, ext_cfg["alignment_timer"])
    state.update(number=timer["timer"], brk=timer["brk"])


@when("they are opened at the alignment frequency in the mode")
def open_for_alignment(fw, ext_cfg, state, mode):
    state.update(frequency=ext_cfg["alignment_frequency"], duties=ext_cfg["alignment_duties"])
    state["pwmclk"] = fw.pwm.open(
        state["number"], pins=[channel["pin"] for channel in state["channels"]], freq=state["frequency"], mode=mode
    )


@when("they get the alignment duties and are recorded, triggered on the first one")
def record_alignment(fw, ad3, ext_cfg, state):
    fw.pwm.duty(state["number"], *state["duties"])
    state["capture"] = record(ad3, ext_cfg, state["frequency"], state["dios"][0])


@when(
    parsers.parse(
        "the channel is opened in {counter_mode} mode at the zero-duty frequency, set to {percent:d} % duty, recorded over the zero-duty "
        "periods and closed"
    )
)
def record_zero_duty(fw, ad3, state, counter_mode, percent):
    number, frequency, periods = state["number"], state["frequency"], state["periods"]
    fw.pwm.open(number, pins=[state["pin"]], freq=frequency, mode=counter_mode, prescaler=state["prescaler"])
    fw.pwm.duty(number, percent)
    capture = ad3.logic.record_for(periods / frequency, timeout=periods / frequency + 2)
    fw.pwm.close(number)
    state.update(capture=capture, highs=analysis.high_low_times(capture.channel(state["dio"]), capture.rate)[0])


@when("the channel is opened at the preload frequency with a 1 MHz counter and the preload")
def open_for_preload(fw, ext_cfg, timer_clock, state, preload):
    state["frequency"] = ext_cfg["preload_frequency"]
    fw.pwm.open(
        state["number"], pins=[state["pin"]], freq=state["frequency"], prescaler=timer_clock // 1_000_000 - 1, preload=bool(preload)
    )


@when(parsers.parse("the duty is set to {percent:d} %"))
def set_duty(fw, state, percent):
    fw.pwm.duty(state["number"], percent)


@when("the logic analyser records the runt window while the duty is set to 80 % and back to 20 % the runt repeats times")
def record_runts(fw, ad3, ext_cfg, state):
    number = state["number"]
    window = ext_cfg["runt_window_s"]
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / window)
    pending = ad3.logic.arm(rate, math.floor(rate * window))
    started = time.monotonic()
    for _ in range(ext_cfg["runt_repeats"]):
        fw.pwm.duty(number, 80)
        fw.pwm.duty(number, 20)
    elapsed = time.monotonic() - started
    state.update(window=window, elapsed=elapsed, capture=pending.wait(timeout=window + 2))


@when("the break input is driven low")
def break_low(ad3, state):
    ad3.dio.drive(state["brk_dio"], 0)


@when("the channel is opened at the break frequency with the break input active high and the filter of the case")
def open_with_break_filter(fw, ext_cfg, state, case):
    state["frequency"] = ext_cfg["break_frequency"]
    fw.pwm.open(state["number"], pins=[state["pin"]], freq=state["frequency"], brk=state["brk"], brkpol="high", brkfilter=case["filter"])


@when("the AD3 pulses the break input once for the ticks of the case and the break settle time passes")
def pulse_break(ad3, ext_cfg, timer_clock, state, case):
    width = case["ticks"] / timer_clock
    ad3.pattern.pulses(state["brk_dio"], 1, 0.5 / width, 0.5)
    ad3.pattern.wait_done(timeout=2)
    time.sleep(ext_cfg["break_settle_s"])
    state["width"] = width


@when("the channel is opened at the TRGO frequency with the TRGO source")
def open_for_trgo(fw, ext_cfg, state, source):
    state.update(frequency=ext_cfg["trgo_frequency"], runs=ext_cfg["trgo_runs"])
    state["pwmclk"] = fw.pwm.open(state["number"], pins=[state["entry"]["channels"][0]["pin"]], freq=state["frequency"], trgo=source)


@when("the ADC opens on its first input, triggered by the TRGO of the timer")
def adc_on_trgo(fw, state):
    fw.adc.open(state["adc"], pins=[state["adc_pin"]], trgo=state["number"])


@when("the shortest round trips of one conversion and of the TRGO runs, less their replies, are measured")
def measure_trgo(fw, board_cfg, ext_cfg, state):
    baud, repeats = board_cfg.terminal.baud, ext_cfg["trgo_repeats"]
    single = timed_measure(fw, state["adc"], 1, repeats, baud)
    state["measured"] = timed_measure(fw, state["adc"], state["runs"], repeats, baud) - single


@then("both channels run at the quantised frequency and duty of the waveform of the mode")
def alignment_waveforms(ext_cfg, state, mode):
    capture, pwmclk, frequency = state["capture"], state["pwmclk"], state["frequency"]
    tolerance = ext_cfg["tolerance"]
    alignment = ALIGNMENTS[mode]
    expected_frequency = expect.pwm_frequency(pwmclk, frequency, alignment)
    for dio, duty in zip(state["dios"], state["duties"]):
        assert capture.frequency(dio) == pytest.approx(expected_frequency, rel=tolerance["frequency"]), f"DIO{dio} frequency"
        quantisation = 100 * 2 * frequency / capture.rate + expect.pwm_duty_step(pwmclk, frequency, alignment)
        expected_duty = expect.pwm_duty(pwmclk, frequency, alignment, duty)
        assert capture.duty(dio) * 100 == pytest.approx(expected_duty, abs=tolerance["duty"] + quantisation), f"DIO{dio} duty"


@then("their rising edges line up counting up, their falling edges counting down and their pulse centres centre aligned")
def alignment_edges(ext_cfg, state, mode):
    capture, dios = state["capture"], state["dios"]
    feature = FEATURES.get(mode, "center")
    reference = feature_times(capture, dios[0], feature)[1:-1]
    assert reference, "no complete pulses captured"
    offsets = analysis.nearest_offsets(reference, feature_times(capture, dios[1], feature))
    worst = max(abs(offset) for offset in offsets)
    assert worst <= ext_cfg["tolerance"]["alignment_s"] + 2 / capture.rate, f"{feature} edges off by {worst * 1e9:.0f} ns"


@then("it stays low without an edge")
def zero_duty_stays_low(state):
    capture, dio = state["capture"], state["dio"]
    assert capture.edge_count(dio) == 0 and capture.channel(dio)[0] == 0, "edge aligned 0 % must stay low"


@then("it is high for one counter tick in every period but two at most")
def zero_duty_one_tick(state):
    highs, periods, tick = state["highs"], state["periods"], state["tick"]
    assert len(highs) >= periods - 2, f"{len(highs)} pulses in {periods} periods"
    assert statistics.median(highs) == pytest.approx(tick, abs=0.1 * tick + 2 / state["capture"].rate)


@then(
    parsers.parse(
        "every mode but edge, trgo update and trgo oc1ref open on the first channel of the timer exactly when it has a counter mode "
        'select, else fail with "{reason}"'
    )
)
def master_timer_needed(fw, timer, reason):
    number, pin = timer["timer"], timer["channels"][0]["pin"]
    supported = expect.timer_has_center_mode(number)
    cases = [{"mode": mode} for mode in ALIGNMENTS if mode != "edge"] + [{"trgo": "update"}, {"trgo": "oc1ref"}]
    for options in cases:
        try:
            fw.pwm.open(number, pins=[pin], **options)
        except FirmwareError as error:
            assert error.reason == reason and not supported, f"{options}: ERR {error.reason}"
            continue
        assert supported, f"{options} opened on TIM{number}"
        fw.pwm.close(number)


@then("the writes took less than the recording")
def writes_within_window(state):
    elapsed, window = state["elapsed"], state["window"]
    assert elapsed < window, f"the writes took {elapsed:.3f} s, longer than the {window} s recording"


@then("pulses were captured")
def pulses_captured(state):
    capture = state["capture"]
    state["highs"] = analysis.high_low_times(capture.channel(state["dio"]), capture.rate)[0]
    assert state["highs"], "no pulses captured"


@then("with preload no pulse lies strictly between 20 % and 80 % beyond the runt margin, without preload some pulse does")
def runts_match_preload(ext_cfg, state, preload):
    capture, highs = state["capture"], state["highs"]
    period = 1 / state["frequency"]
    margin = ext_cfg["runt_margin"] * period + 2 / capture.rate
    runts = [high for high in highs if 0.2 * period + margin < high < 0.8 * period - margin]
    if preload:
        assert not runts, f"preload=1: pulses of {[round(high / period * 100, 1) for high in runts]} %"
    else:
        assert runts, f"preload=0: no cut pulse in {ext_cfg['runt_repeats']} writes"


@then("the channel switches before the pulse")
def runs_before_pulse(ad3, ext_cfg, state):
    assert record(ad3, ext_cfg, state["frequency"]).edge_count(state["dio"]) > 0, "outputs must run before the pulse"


@then("the channel stops switching exactly when the case trips")
def trips_with_case(ad3, ext_cfg, state, case):
    width, filter_ticks = state["width"], state["filter_ticks"]
    edges = record(ad3, ext_cfg, state["frequency"]).edge_count(state["dio"])
    assert (edges == 0) is case["trips"], f"{width * 1e6:.2f} us pulse, filter {filter_ticks} clocks: {edges} edges after it"


@then("the extra runs take one period each, within the TRGO tolerance and jitter")
def trgo_paces(ext_cfg, state):
    frequency, runs, measured = state["frequency"], state["runs"], state["measured"]
    expected = (runs - 1) / expect.pwm_frequency(state["pwmclk"], frequency, "edge")
    tolerance, jitter = ext_cfg["trgo_tolerance"], ext_cfg["trgo_jitter_s"]
    assert (1 - tolerance) * expected - jitter <= measured <= (1 + tolerance) * expected + jitter, (
        f"{runs - 1} more runs took {measured * 1000:.2f} ms, {expected * 1000:.2f} ms at {frequency} Hz"
    )


@then("every invalid ADC trgo setting fails with its reason")
def trgo_settings_refused(fw, state):
    adc, pin, number = state["adc"], state["adc_pin"], state["number"]
    cases = [
        ({"trgo": number}, "unsupported"),
        ({"trgo": number, "timer": number}, "usage"),
        ({"trgo": number, "rate": 1000}, "usage"),
        ({"trgo": 18}, "range"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.adc.open(adc, pins=[pin], **options)
        assert error.value.reason == reason, options


@then(parsers.parse('an ADC on the TRGO of each timer that has none fails with "{reason}" while the timer runs'))
def trgo_unsupported_timers(fw, board_cfg, ext_cfg, state, reason):
    adc, pin = state["adc"], state["adc_pin"]
    for name in ext_cfg["trgo_unsupported_timers"]:
        entry = pwm_timer(board_cfg, name)
        fw.pwm.open(entry["timer"], pins=[entry["channels"][0]["pin"]])
        with pytest.raises(FirmwareError) as error:
            fw.adc.open(adc, pins=[pin], trgo=entry["timer"])
        assert error.value.reason == reason, name
        fw.pwm.close(entry["timer"])


@then(parsers.parse('an ADC on the TRGO of the timer fails with "{reason}" while its encoder holds it, if the timer has one'))
def trgo_busy_with_encoder(fw, board_cfg, state, reason):
    adc, pin, number = state["adc"], state["adc_pin"], state["number"]
    encoder = next((instance for instance in board_cfg.param("qei.instances") if instance["index"] == number), None)
    if encoder is not None:
        fw.qei.open(number, a=encoder["a"], b=encoder["b"])
        with pytest.raises(FirmwareError) as error:
            fw.adc.open(adc, pins=[pin], trgo=number)
        assert error.value.reason == reason, "the encoder holds the timer"


@then("every invalid break filter, preload, mode and trgo setting fails with its reason")
def open_settings_refused(fw, state):
    number, brk = state["number"], state["brk"]
    cases = [
        ({"brkfilter": 3}, "usage"),
        ({"brkfilter": 16}, "range"),
        ({"brkfilter": "x", "brk": brk}, "usage"),
        ({"brkfilter": 16, "brk": brk}, "range"),
        ({"preload": 2}, "range"),
        ({"mode": "edgeup"}, "usage"),
        ({"trgo": "oc5ref"}, "usage"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.command("pwm.open", number, channels=1, **options)
        assert error.value.reason == reason, options


@then("the timer opens on channel 1 with the break input, break filter 15, no preload, centerboth mode and trgo oc4ref")
def all_settings_open(fw, state):
    fw.pwm.open(state["number"], channels=[1], brk=state["brk"], brkfilter=15, preload=False, mode="centerboth", trgo="oc4ref")
