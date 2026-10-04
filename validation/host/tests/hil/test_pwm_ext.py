"""PWM extensions (`hal::PwmStm`, PROTOCOL.md D.10): the five counter modes, compare preload, the break input
filter, and the trigger output an `adc.open trgo=` sequence runs on.

Wiring set `bundle1`: the outputs and break inputs of `tests.pwm.timers` on DIOs (`tests.pwm_ext` names the
entries); the TRGO and argument tests need no AD3.
"""

from __future__ import annotations

import math
import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

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


@pytest.mark.ad3
@pytest.mark.board_params("mode", "pwm_ext.modes")
def test_alignment(fw, ad3, need, board_cfg, ext_cfg, mode):
    """Every mode gives the frequency and duty of its waveform; two outputs of one timer share their rising edges
    counting up, their falling edges counting down (`edgedown`) and their pulse centres centre aligned."""
    timer = pwm_timer(board_cfg, ext_cfg["alignment_timer"])
    channels = timer["channels"][:2]
    dios = [need.dio(channel["pin"]) for channel in channels]
    frequency, duties = ext_cfg["alignment_frequency"], ext_cfg["alignment_duties"]
    tolerance = ext_cfg["tolerance"]
    pwmclk = fw.pwm.open(timer["timer"], pins=[channel["pin"] for channel in channels], freq=frequency, mode=mode)
    fw.pwm.duty(timer["timer"], *duties)
    capture = record(ad3, ext_cfg, frequency, dios[0])
    alignment = ALIGNMENTS[mode]
    expected_frequency = expect.pwm_frequency(pwmclk, frequency, alignment)
    for dio, duty in zip(dios, duties):
        assert capture.frequency(dio) == pytest.approx(expected_frequency, rel=tolerance["frequency"]), f"DIO{dio} frequency"
        quantisation = 100 * 2 * frequency / capture.rate + expect.pwm_duty_step(pwmclk, frequency, alignment)
        expected_duty = expect.pwm_duty(pwmclk, frequency, alignment, duty)
        assert capture.duty(dio) * 100 == pytest.approx(expected_duty, abs=tolerance["duty"] + quantisation), f"DIO{dio} duty"
    feature = FEATURES.get(mode, "center")
    reference = feature_times(capture, dios[0], feature)[1:-1]
    assert reference, "no complete pulses captured"
    offsets = analysis.nearest_offsets(reference, feature_times(capture, dios[1], feature))
    worst = max(abs(offset) for offset in offsets)
    assert worst <= tolerance["alignment_s"] + 2 / capture.rate, f"{feature} edges off by {worst * 1e9:.0f} ns"


@pytest.mark.ad3
def test_edgedown_cannot_reach_zero(fw, ad3, need, board_cfg, ext_cfg, timer_clock):
    """At 0 % `mode=edge` stays low, while `mode=edgedown` keeps one counter tick high per period: counting down,
    PWM mode 1 is active while CNT <= CCR, so CCR = 0 still matches once (PwmStm.hpp)."""
    settings = ext_cfg["zero_duty"]
    timer = pwm_timer(board_cfg, ext_cfg["alignment_timer"])
    pin = timer["channels"][0]["pin"]
    dio = need.dio(pin)
    tick = settings["tick_us"] / 1e6
    frequency, periods = settings["frequency"], settings["periods"]
    prescaler = round(timer_clock * tick) - 1
    for mode in ("edge", "edgedown"):
        fw.pwm.open(timer["timer"], pins=[pin], freq=frequency, mode=mode, prescaler=prescaler)
        fw.pwm.duty(timer["timer"], 0)
        capture = ad3.logic.record_for(periods / frequency, timeout=periods / frequency + 2)
        fw.pwm.close(timer["timer"])
        highs = analysis.high_low_times(capture.channel(dio), capture.rate)[0]
        if mode == "edge":
            assert capture.edge_count(dio) == 0 and capture.channel(dio)[0] == 0, "edge aligned 0 % must stay low"
            continue
        assert len(highs) >= periods - 2, f"{len(highs)} pulses in {periods} periods"
        assert statistics.median(highs) == pytest.approx(tick, abs=0.1 * tick + 2 / capture.rate)


@pytest.mark.board_params("timer", "pwm.timers")
def test_modes_and_trgo_need_a_master_timer(fw, timer):
    """Every mode but `edge` needs a counter mode select and `trgo` a master mode (both: TIM1, TIM2 and on
    STM32WBA55 TIM3); TIM16/TIM17 answer `ERR unsupported`, the others open."""
    number, pin = timer["timer"], timer["channels"][0]["pin"]
    supported = expect.timer_has_center_mode(number)
    cases = [{"mode": mode} for mode in ALIGNMENTS if mode != "edge"] + [{"trgo": "update"}, {"trgo": "oc1ref"}]
    for options in cases:
        try:
            fw.pwm.open(number, pins=[pin], **options)
        except FirmwareError as error:
            assert error.reason == "unsupported" and not supported, f"{options}: ERR {error.reason}"
            continue
        assert supported, f"{options} opened on TIM{number}"
        fw.pwm.close(number)


@pytest.mark.ad3
@pytest.mark.parametrize("preload", [0, 1])
def test_preload(fw, ad3, need, board_cfg, ext_cfg, timer_clock, preload):
    """Duty 80 % then 20 %, written `runt_repeats` times while the LA records: without preload a write lands
    mid-period and cuts or stretches that period's pulse (some high pulse lies strictly between 20 % and 80 %);
    with preload every period shows 20 % or 80 %."""
    timer = pwm_timer(board_cfg, ext_cfg["preload_timer"])
    number, pin = timer["timer"], timer["channels"][0]["pin"]
    dio = need.dio(pin)
    frequency = ext_cfg["preload_frequency"]
    fw.pwm.open(number, pins=[pin], freq=frequency, prescaler=timer_clock // 1_000_000 - 1, preload=bool(preload))
    fw.pwm.duty(number, 20)
    window = ext_cfg["runt_window_s"]
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / window)
    pending = ad3.logic.arm(rate, math.floor(rate * window))
    started = time.monotonic()
    for _ in range(ext_cfg["runt_repeats"]):
        fw.pwm.duty(number, 80)
        fw.pwm.duty(number, 20)
    elapsed = time.monotonic() - started
    capture = pending.wait(timeout=window + 2)
    assert elapsed < window, f"the writes took {elapsed:.3f} s, longer than the {window} s recording"
    period = 1 / frequency
    margin = ext_cfg["runt_margin"] * period + 2 / capture.rate
    highs = analysis.high_low_times(capture.channel(dio), capture.rate)[0]
    assert highs, "no pulses captured"
    runts = [high for high in highs if 0.2 * period + margin < high < 0.8 * period - margin]
    if preload:
        assert not runts, f"preload=1: pulses of {[round(high / period * 100, 1) for high in runts]} %"
    else:
        assert runts, f"preload=0: no cut pulse in {ext_cfg['runt_repeats']} writes"


@pytest.mark.ad3
@pytest.mark.board_params("name", "pwm_ext.break_filter_timers")
@pytest.mark.board_params("case", "pwm_ext.break_filter_cases")
def test_break_filter(fw, ad3, need, board_cfg, ext_cfg, timer_clock, name, case):
    """`brkfilter` (BDTR.BKF) ignores a break pulse shorter than the filter: one AD3 pulse of `ticks` kernel clocks
    trips the break (outputs off, `brkauto=0`) exactly when it outlasts the filter; without a filter even the
    short pulse trips it."""
    timer = pwm_timer(board_cfg, name)
    pin, brk = timer["channels"][0]["pin"], timer.get("brk")
    if brk is None:
        pytest.skip(f"{name} has no wired break input")
    dio, brk_dio = need.dio(pin), need.dio(brk)
    filter_ticks = break_filter_ticks(case["filter"])
    assert case["trips"] == (case["ticks"] > filter_ticks), "tests.pwm_ext.break_filter_cases contradicts BKF"
    frequency = ext_cfg["break_frequency"]
    ad3.dio.drive(brk_dio, 0)
    fw.pwm.open(timer["timer"], pins=[pin], freq=frequency, brk=brk, brkpol="high", brkfilter=case["filter"])
    fw.pwm.duty(timer["timer"], 50)
    assert record(ad3, ext_cfg, frequency).edge_count(dio) > 0, "outputs must run before the pulse"
    width = case["ticks"] / timer_clock
    ad3.pattern.pulses(brk_dio, 1, 0.5 / width, 0.5)
    ad3.pattern.wait_done(timeout=2)
    time.sleep(ext_cfg["break_settle_s"])
    edges = record(ad3, ext_cfg, frequency).edge_count(dio)
    assert (edges == 0) is case["trips"], f"{width * 1e6:.2f} us pulse, filter {filter_ticks} clocks: {edges} edges after it"


def timed_measure(fw, adc, runs, repeats, baud):
    """Shortest of `repeats` round trips of `adc.measure n=<runs>`, less the transmission of its reply."""
    shortest = math.inf
    for _ in range(repeats):
        start = time.monotonic()
        samples = fw.adc.measure(adc, n=runs, cmd_timeout=3.0)
        shortest = min(shortest, time.monotonic() - start)
        assert len(samples) == runs
    return shortest - expect.uart_transfer_time(len(",".join(str(sample) for sample in samples)), baud)


@pytest.mark.matrix("pwm_ext.trgo")
def test_trgo_paces_adc(fw, board_cfg, ext_cfg, need, timer, source):
    """`adc.open trgo=<t>` converts once per TRGO of the timer the pwm group drives: with `trgo=update` or
    `trgo=oc1ref` once per period, so `adc.measure n=<runs>` takes (runs - 1) periods longer than `n=1`."""
    entry = pwm_timer(board_cfg, timer)
    adc_cfg = board_cfg.param("adc")
    adc, pin = adc_cfg["adc"], adc_cfg["inputs"][0]
    need.unloaded(pin, entry["channels"][0]["pin"])
    frequency, runs = ext_cfg["trgo_frequency"], ext_cfg["trgo_runs"]
    pwmclk = fw.pwm.open(entry["timer"], pins=[entry["channels"][0]["pin"]], freq=frequency, trgo=source)
    fw.adc.open(adc, pins=[pin], trgo=entry["timer"])
    fw.pwm.duty(entry["timer"], 50)
    baud, repeats = board_cfg.terminal.baud, ext_cfg["trgo_repeats"]
    single = timed_measure(fw, adc, 1, repeats, baud)
    measured = timed_measure(fw, adc, runs, repeats, baud) - single
    expected = (runs - 1) / expect.pwm_frequency(pwmclk, frequency, "edge")
    tolerance, jitter = ext_cfg["trgo_tolerance"], ext_cfg["trgo_jitter_s"]
    assert (1 - tolerance) * expected - jitter <= measured <= (1 + tolerance) * expected + jitter, (
        f"{runs - 1} more runs took {measured * 1000:.2f} ms, {expected * 1000:.2f} ms at {frequency} Hz"
    )


def test_trgo_errors(fw, board_cfg, ext_cfg):
    """`trgo` excludes `timer` and `rate` (`ERR usage`); a timer the MCU lacks is `ERR range`; a timer nobody drives,
    or one whose TRGO the ADC cannot trigger from, is `ERR unsupported`; one another group holds is `ERR busy`."""
    adc_cfg = board_cfg.param("adc")
    adc, pin = adc_cfg["adc"], adc_cfg["inputs"][0]
    number = pwm_timer(board_cfg, ext_cfg["trgo"]["timer"][0])["timer"]
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
    for name in ext_cfg["trgo_unsupported_timers"]:
        entry = pwm_timer(board_cfg, name)
        fw.pwm.open(entry["timer"], pins=[entry["channels"][0]["pin"]])
        with pytest.raises(FirmwareError) as error:
            fw.adc.open(adc, pins=[pin], trgo=entry["timer"])
        assert error.value.reason == "unsupported", name
        fw.pwm.close(entry["timer"])
    encoder = next((instance for instance in board_cfg.param("qei.instances") if instance["index"] == number), None)
    if encoder is not None:
        fw.qei.open(number, a=encoder["a"], b=encoder["b"])
        with pytest.raises(FirmwareError) as error:
            fw.adc.open(adc, pins=[pin], trgo=number)
        assert error.value.reason == "busy", "the encoder holds the timer"


def test_open_errors(fw, board_cfg, ext_cfg):
    """`brkfilter` 0-15 needs `brk` (`ERR usage`), `preload` is 0 or 1, `mode` and `trgo` take their names only."""
    timer = pwm_timer(board_cfg, ext_cfg["alignment_timer"])
    number, brk = timer["timer"], timer["brk"]
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
    fw.pwm.open(number, channels=[1], brk=brk, brkfilter=15, preload=False, mode="centerboth", trgo="oc4ref")
