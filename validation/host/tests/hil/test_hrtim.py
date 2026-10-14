"""High-resolution timer (`hal::HighResolutionPwmStm`) through the `hrtim` group: prescaler and period, outputs in
phase with one or two duties, complementary with dead time and shifted by a phase, the fault input, the ADC trigger
and the pins held while a unit is open.

Scenarios: features/hrtim.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, scenario, then, when

from hal_st_validation.groups.hrtim import compare_ticks, dead_time, duty_cycle, frequency_range, timing_for


@pytest.fixture
def hrtim_cfg(board_cfg):
    return board_cfg.param("hrtim")


@pytest.fixture(scope="module")
def timer_clock(board_cfg):
    return board_cfg.clock("timer")


@pytest.fixture
def state():
    """What the steps of one scenario hand on to the later ones."""
    return {}


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def check_timing(timing, clock, frequency):
    expected = timing_for(clock, frequency)
    assert expected is not None, frequency
    assert (timing.clock, timing.ratio, timing.period) == (clock, *expected), (frequency, timing)


def expected_compare(timing, percent):
    return compare_ticks(timing.period, timing.ratio, duty_cycle(str(percent)))


def record(ad3, hrtim_cfg, frequency, trigger_dio=None, periods=None):
    periods = periods or hrtim_cfg["capture_periods"]
    trigger = None if trigger_dio is None else (trigger_dio, "rising")
    return ad3.logic.record_for(periods / frequency, trigger=trigger, timeout=periods / frequency + 2)


def tolerance(hrtim_cfg, key):
    return hrtim_cfg["tolerance"][key]


def check_output(hrtim_cfg, capture, dio, timing, compare):
    duty = 100 * compare / timing.period
    quantisation = 100 * 2 * timing.frequency / capture.rate
    assert capture.frequency(dio) == pytest.approx(timing.frequency, rel=tolerance(hrtim_cfg, "frequency")), f"DIO{dio} frequency"
    assert capture.duty(dio) * 100 == pytest.approx(duty, abs=tolerance(hrtim_cfg, "duty") + quantisation), f"DIO{dio} duty"


@pytest.mark.board_params("freq", "hrtim.timings")
@scenario("hrtim.feature", "The prescaler and period follow the frequency")
def test_timings(freq):
    pass


@scenario("hrtim.feature", "The frequency limits of the counter open and change, one step beyond is refused")
def test_frequency_limits():
    pass


@pytest.mark.board_params("dead", "hrtim.dead_ns")
@scenario("hrtim.feature", "The dead time picks the finest prescaler that holds it")
def test_dead_time_replies(dead):
    pass


@scenario("hrtim.feature", "The duty and the frequency change on a running unit")
def test_changes():
    pass


@scenario("hrtim.feature", "Two duties need both outputs in phase, a phase needs the shifted outputs")
def test_channels_and_phase():
    pass


@pytest.mark.board_params("freq, duty", "hrtim.waveforms")
@scenario("hrtim.feature", "Both outputs run at the frequency and duty")
def test_waveform(freq, duty):
    pass


@pytest.mark.board_params("duty1, duty2", "hrtim.two_duties.cases")
@scenario("hrtim.feature", "Both outputs in phase run at their own duty")
def test_two_duties(duty1, duty2):
    pass


@pytest.mark.board_params("dead", "hrtim.complementary.dead_ns")
@scenario("hrtim.feature", "The complementary outputs switch over after the dead time")
def test_complementary(dead):
    pass


@pytest.mark.board_params("phase", "hrtim.shifted.phases")
@scenario("hrtim.feature", "Output 1 follows output 2 by the phase")
def test_phase(phase):
    pass


@scenario("hrtim.feature", "A fault forces the outputs inactive until the unit starts again")
def test_fault():
    pass


@pytest.mark.board_params("freq", "hrtim.adc.frequencies")
@scenario("hrtim.feature", "The ADC converts on the trigger of the unit period")
def test_adc(freq):
    pass


@scenario("hrtim.feature", "The ADC count needs a trigger ADC1 takes and a running unit")
def test_adc_refused():
    pass


@scenario("hrtim.feature", "The pins of a unit are held while it is open")
def test_ownership():
    pass


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("hrtim.fault 1", "usage"),
        ("hrtim.fault 1 clear=1", "usage"),
        ("hrtim.adc 1 100", "usage"),
        ("hrtim.adc 1 1", "range"),
        ("hrtim.adc 1 1001", "range"),
        ("hrtim.phase 1 10", "usage"),
        ("hrtim.duty 1", "usage"),
        ("hrtim.duty 1 101", "usage"),
        ("hrtim.duty 1 12.34567", "usage"),
        ("hrtim.duty 1 0x10", "usage"),
        ("hrtim.duty 1 50 50 50", "usage"),
        ("hrtim.duty 1 50 x", "usage"),
        ("hrtim.freq 1 0", "range"),
        ("hrtim.freq 1 648", "range"),
        ("hrtim.freq 1 28333334", "range"),
        ("hrtim.duty 2 50", "notopen"),
        ("hrtim.open 1", "busy"),
        ("hrtim.open 2", "busy"),
    ],
)
@scenario("hrtim.feature", "Commands that need an option or another mode are refused")
def test_state_errors(line, reason):
    pass


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("hrtim.open", "usage"),
        ("hrtim.open 1 bogus=1", "usage"),
        ("hrtim.open 1 duty=50", "usage"),
        ("hrtim.open 1 2", "usage"),
        ("hrtim.open 0", "range"),
        ("hrtim.open 7", "range"),
        ("hrtim.open 1 out=3", "usage"),
        ("hrtim.open 1 mode=x", "usage"),
        ("hrtim.open 1 freq=0", "range"),
        ("hrtim.open 1 freq=648", "range"),
        ("hrtim.open 1 freq=28333334", "range"),
        ("hrtim.open 1 freq=4294967296", "usage"),
        ("hrtim.open 1 dead=50", "usage"),
        ("hrtim.open 1 deadfall=50", "usage"),
        ("hrtim.open 1 mode=shift dead=50", "usage"),
        ("hrtim.open 1 mode=comp dead=10001", "range"),
        ("hrtim.open 1 flt=PZ1", "pin"),
        ("hrtim.open 1 flt=PA8", "pin"),
        ("hrtim.open 1 fltpol=high", "usage"),
        ("hrtim.open 1 flt=PA12 fltpol=x", "usage"),
        ("hrtim.open 1 adc=0", "range"),
        ("hrtim.open 1 adc=11", "range"),
        ("hrtim.open 5 adc=2", "unsupported"),
        ("hrtim.duty 1 50", "notopen"),
        ("hrtim.stop 1", "notopen"),
        ("hrtim.adc 1 1", "notopen"),
        ("hrtim.close 1", "notopen"),
        ("hrtim.stop 7", "range"),
        ("hrtim.fault 1 bogus=1", "usage"),
    ],
)
@scenario("hrtim.feature", "Malformed and out-of-range commands are refused")
def test_errors(line, reason):
    pass


# replies


@then("opening unit A at the frequency answers the timer clock and the prescaler ratio and period of the model")
def timing_reply(fw, need, hrtim_cfg, timer_clock, freq):
    need.unloaded(hrtim_cfg["out1"], hrtim_cfg["out2"])
    check_timing(fw.hrtim.open(hrtim_cfg["unit"], freq=freq), timer_clock, freq)


@then('opening unit A one below the lowest or one above the highest frequency fails with "range"')
def beyond_limits(fw, need, hrtim_cfg, timer_clock, state):
    need.unloaded(hrtim_cfg["out1"], hrtim_cfg["out2"])
    lowest, highest = frequency_range(timer_clock)
    state.update(lowest=lowest, highest=highest)
    for frequency in (lowest - 1, highest + 1):
        expect_reason(fw, f"hrtim.open {hrtim_cfg['unit']} freq={frequency}", "range")


@when("unit A is opened at the lowest frequency")
def open_at_lowest(fw, hrtim_cfg, timer_clock, state):
    check_timing(fw.hrtim.open(hrtim_cfg["unit"], freq=state["lowest"]), timer_clock, state["lowest"])


@then("changing the frequency to the highest answers the timing of the model")
def change_to_highest(fw, hrtim_cfg, timer_clock, state):
    check_timing(fw.hrtim.freq(hrtim_cfg["unit"], state["highest"]), timer_clock, state["highest"])


@then('changing the frequency to one above the highest fails with "range"')
def change_beyond_highest(fw, hrtim_cfg, state):
    expect_reason(fw, f"hrtim.freq {hrtim_cfg['unit']} {state['highest'] + 1}", "range")


@then("opening unit A complementary with the dead time answers the dead-time prescaler and ticks of the model")
def dead_time_reply(fw, need, hrtim_cfg, timer_clock, dead):
    need.unloaded(hrtim_cfg["out1"], hrtim_cfg["out2"])
    timing = fw.hrtim.open(hrtim_cfg["unit"], mode="comp", dead=dead)
    prescaler, ticks, _ = dead_time(timer_clock, dead, dead)
    assert (timing.dtprsc, timing.dead) == (prescaler, ticks), (dead, timing)
    assert abs(ticks * timing.dead_tick_s - dead * 1e-9) <= timing.dead_tick_s / 2, (dead, timing)


@when("unit A is opened at the first of the frequency changes")
def open_for_changes(fw, need, hrtim_cfg, state):
    need.unloaded(hrtim_cfg["out1"], hrtim_cfg["out2"])
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], freq=hrtim_cfg["frequency_changes"][0])


@then("every duty change answers the compares of the model for both outputs")
def duty_changes(fw, hrtim_cfg, state):
    timing = state["timing"]
    for duty in hrtim_cfg["duty_changes"]:
        assert fw.hrtim.duty(hrtim_cfg["unit"], duty) == [expected_compare(timing, duty)] * 2, duty


@then("every frequency change answers the timing of the model and keeps the duty")
def frequency_changes(fw, hrtim_cfg, timer_clock):
    duty = hrtim_cfg["duty_changes"][-1]
    for frequency in hrtim_cfg["frequency_changes"][1:]:
        timing = fw.hrtim.freq(hrtim_cfg["unit"], frequency)
        check_timing(timing, timer_clock, frequency)
        assert fw.hrtim.duty(hrtim_cfg["unit"], duty) == [expected_compare(timing, duty)] * 2, frequency


@when("unit A is opened complementary")
def open_complementary_plain(fw, need, hrtim_cfg, state):
    need.unloaded(hrtim_cfg["out1"], hrtim_cfg["out2"])
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], mode="comp")


@then('two duties answer "usage" and one duty answers its compare')
def one_channel_only(fw, hrtim_cfg, state):
    expect_reason(fw, f"hrtim.duty {hrtim_cfg['unit']} 30 70", "usage")
    assert fw.hrtim.duty(hrtim_cfg["unit"], 30) == [expected_compare(state["timing"], 30)]


@when("unit A is reopened with output 1 only")
def reopen_output_1(fw, hrtim_cfg):
    fw.hrtim.close(hrtim_cfg["unit"])
    fw.hrtim.open(hrtim_cfg["unit"], out="1")


@then('two duties answer "usage"')
def two_duties_refused(fw, hrtim_cfg):
    expect_reason(fw, f"hrtim.duty {hrtim_cfg['unit']} 30 70", "usage")


@when("unit A is reopened shifted")
def reopen_shifted(fw, hrtim_cfg):
    fw.hrtim.close(hrtim_cfg["unit"])
    fw.hrtim.open(hrtim_cfg["unit"], mode="shift")


@then('a phase of 100 % answers "range" and one of 99.99 % succeeds')
def phase_limits(fw, hrtim_cfg):
    expect_reason(fw, f"hrtim.phase {hrtim_cfg['unit']} 100", "range")
    fw.hrtim.phase(hrtim_cfg["unit"], "99.99")


# waveforms


@given("the outputs of unit A are wired")
def outputs_wired(need, hrtim_cfg, state):
    state.update(a=need.dio(hrtim_cfg["out1"]), b=need.dio(hrtim_cfg["out2"]))


@given("the outputs of unit A and FLT1 are wired")
def outputs_and_fault_wired(need, hrtim_cfg, state):
    state.update(a=need.dio(hrtim_cfg["out1"]), b=need.dio(hrtim_cfg["out2"]), flt=need.dio(hrtim_cfg["fault_pin"]))


@when("unit A opens in phase at the frequency and starts with the duty")
def open_in_phase(fw, hrtim_cfg, state, freq, duty):
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], freq=freq)
    state["compares"] = fw.hrtim.duty(hrtim_cfg["unit"], duty)


@when("unit A opens in phase at the two-duty frequency and starts with both duties")
def open_two_duties(fw, hrtim_cfg, state, duty1, duty2):
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], freq=hrtim_cfg["two_duties"]["frequency"])
    state["compares"] = fw.hrtim.duty(hrtim_cfg["unit"], duty1, duty2)
    assert state["compares"] == [expected_compare(state["timing"], duty1), expected_compare(state["timing"], duty2)]


@when("unit A opens complementary at the complementary frequency with the dead time and starts")
def open_complementary(fw, hrtim_cfg, state, dead):
    case = hrtim_cfg["complementary"]
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], mode="comp", freq=case["frequency"], dead=dead)
    fw.hrtim.duty(hrtim_cfg["unit"], case["duty"])


@when("unit A opens shifted at the phase frequency, gets the phase and starts")
def open_shifted(fw, hrtim_cfg, state, phase):
    case = hrtim_cfg["shifted"]
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], mode="shift", freq=case["frequency"])
    fw.hrtim.phase(hrtim_cfg["unit"], phase)
    fw.hrtim.duty(hrtim_cfg["unit"], case["duty"])


@when("both outputs are recorded")
def record_both(ad3, hrtim_cfg, state):
    state["capture"] = record(ad3, hrtim_cfg, state["timing"].frequency, state["a"])


@when("both outputs are recorded at the dead-time resolution")
def record_fine(ad3, hrtim_cfg, state):
    state["capture"] = record(ad3, hrtim_cfg, state["timing"].frequency, state["b"], hrtim_cfg["fine_capture_periods"])


def check_outputs(hrtim_cfg, state):
    for dio, compare in zip((state["a"], state["b"]), state["compares"], strict=True):
        check_output(hrtim_cfg, state["capture"], dio, state["timing"], compare)


@then("both outputs run at the frequency of the reported period and the duty of the reported compare")
def waveform_matches(hrtim_cfg, state):
    check_outputs(hrtim_cfg, state)


@then("each output runs at the duty of its reported compare and both rise together")
def two_duties_match(hrtim_cfg, state):
    capture = state["capture"]
    check_outputs(hrtim_cfg, state)
    reference = analysis.rising_times(capture.channel(state["a"]), capture.rate)[1:-1]
    assert reference, "no complete pulses captured"
    offsets = analysis.nearest_offsets(reference, analysis.rising_times(capture.channel(state["b"]), capture.rate))
    assert max(abs(offset) for offset in offsets) <= 2 / capture.rate


@then("the outputs are never active together")
def no_overlap(state):
    capture = state["capture"]
    assert analysis.overlap_samples(capture.channel(state["a"]), capture.channel(state["b"])) == 0


@then("each switch-over waits the reported dead time")
def dead_times_match(hrtim_cfg, state):
    capture, timing = state["capture"], state["timing"]
    expected = timing.dead * timing.dead_tick_s
    allowed = tolerance(hrtim_cfg, "dead_s") + 2 / capture.rate
    split = analysis.dead_times_split(capture.channel(state["a"]), capture.channel(state["b"]), capture.rate)
    for name, times in (("output 1 off to output 2 on", split.a_off_to_b_on), ("output 2 off to output 1 on", split.b_off_to_a_on)):
        assert times, f"no switch-over {name}"
        worst = max(abs(measured - expected) for measured in times)
        assert worst <= allowed, f"{name}: off by {worst * 1e9:.0f} ns from {expected * 1e9:.0f} ns"


@then("the rising edges of output 1 follow those of output 2 by the phase")
def phase_matches(hrtim_cfg, state, phase):
    capture, timing = state["capture"], state["timing"]
    expected = expected_compare(timing, phase) * timing.tick_s
    delay, _ = capture.phase(state["b"], state["a"])
    allowed = tolerance(hrtim_cfg, "phase_s") + 2 / capture.rate
    assert abs(delay - expected) <= allowed, f"phase delay {delay * 1e9:.0f} ns, expected {expected * 1e9:.0f} ns"


# fault


@given("FLT1 is driven high")
def fault_high(ad3, state):
    ad3.dio.drive(state["flt"], 1)


@when("unit A opens in phase with FLT1 active low and starts")
def open_with_fault(fw, hrtim_cfg, state):
    case = hrtim_cfg["waveforms"][0]
    state["duty"] = case["duty"]
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], freq=case["freq"], flt=hrtim_cfg["fault_pin"], fltpol="low")
    fw.hrtim.duty(hrtim_cfg["unit"], state["duty"])


@then("both outputs switch and no fault is active")
def outputs_switch(fw, ad3, hrtim_cfg, state):
    capture = record(ad3, hrtim_cfg, state["timing"].frequency)
    for dio in (state["a"], state["b"]):
        assert capture.edge_count(dio) > 0, f"DIO{dio} does not switch"
    assert not fw.hrtim.fault(hrtim_cfg["unit"])


@when("FLT1 is driven low and then high again")
def fault_pulse(ad3, hrtim_cfg, state):
    ad3.dio.drive(state["flt"], 0)
    time.sleep(hrtim_cfg["fault_settle_s"])
    ad3.dio.drive(state["flt"], 1)
    time.sleep(hrtim_cfg["fault_settle_s"])


@then("a fault is active and both outputs stay low")
def outputs_low(fw, ad3, hrtim_cfg, state):
    assert fw.hrtim.fault(hrtim_cfg["unit"])
    capture = record(ad3, hrtim_cfg, state["timing"].frequency)
    for dio in (state["a"], state["b"]):
        assert capture.edge_count(dio) == 0 and not any(capture.channel(dio)), f"DIO{dio} not held low"


@when("the duty is set again")
def restart(fw, hrtim_cfg, state):
    fw.hrtim.duty(hrtim_cfg["unit"], state["duty"])


# ADC trigger


@when("unit A opens with one output at the ADC frequency with the ADC trigger and starts")
def open_with_adc(fw, need, hrtim_cfg, state, freq):
    need.unloaded(hrtim_cfg["out1"])
    state["timing"] = fw.hrtim.open(hrtim_cfg["unit"], out="1", freq=freq, adc=hrtim_cfg["adc"]["trigger"])
    fw.hrtim.duty(hrtim_cfg["unit"], 50)


@then("hrtim.adc counts conversions at the frequency of the reported period")
def adc_rate(fw, hrtim_cfg, state):
    samples = hrtim_cfg["adc"]["samples"]
    us = fw.hrtim.adc(hrtim_cfg["unit"], samples)
    measured = (samples - 1) * 1e6 / us
    allowed = tolerance(hrtim_cfg, "adc_rate") + 1 / us
    assert measured == pytest.approx(state["timing"].frequency, rel=allowed), (measured, state["timing"].frequency)


@when("unit A is opened with one output and ADC trigger 2")
def open_with_trigger_2(fw, need, hrtim_cfg):
    need.unloaded(hrtim_cfg["out1"])
    fw.hrtim.open(hrtim_cfg["unit"], out="1", adc=2)


@then('hrtim.adc answers "unsupported"')
def adc_unsupported(fw, hrtim_cfg):
    expect_reason(fw, f"hrtim.adc {hrtim_cfg['unit']} 10", "unsupported")


@when("unit A is reopened with ADC trigger 1 and not started")
def reopen_with_trigger_1(fw, hrtim_cfg):
    fw.hrtim.close(hrtim_cfg["unit"])
    fw.hrtim.open(hrtim_cfg["unit"], out="1", adc=1)


@then('hrtim.adc answers "timeout"')
def adc_timeout(fw, hrtim_cfg):
    expect_reason(fw, f"hrtim.adc {hrtim_cfg['unit']} 10", "timeout")


# ownership and errors


@when("unit A is opened with FLT1")
def open_with_flt1(fw, need, hrtim_cfg):
    need.unloaded(hrtim_cfg["out1"], hrtim_cfg["out2"], hrtim_cfg["fault_pin"])
    fw.hrtim.open(hrtim_cfg["unit"], flt=hrtim_cfg["fault_pin"])


@then("gpio.cfg of its output and fault pins answers busy")
def pins_busy(fw, hrtim_cfg):
    for pin in (hrtim_cfg["out1"], hrtim_cfg["out2"], hrtim_cfg["fault_pin"]):
        expect_reason(fw, f"gpio.cfg {pin} in", "busy")


@then("hrtim.open of unit B answers busy while unit A is open")
def other_unit_busy(fw, hrtim_cfg):
    expect_reason(fw, f"hrtim.open {hrtim_cfg['other_unit']}", "busy")


@when("unit A is closed")
def close_unit(fw, hrtim_cfg):
    fw.hrtim.close(hrtim_cfg["unit"])


@then("gpio.cfg of its output pins succeeds")
def pins_free(fw, hrtim_cfg):
    for pin in (hrtim_cfg["out1"], hrtim_cfg["out2"]):
        fw.terminal.command(f"gpio.cfg {pin} in")
        fw.terminal.command(f"gpio.release {pin}")


@when("unit A is opened in phase without fault input or ADC trigger")
def open_plain(fw, need, hrtim_cfg):
    need.unloaded(hrtim_cfg["out1"], hrtim_cfg["out2"])
    fw.hrtim.open(hrtim_cfg["unit"])


@then("the command line on the open unit fails with the reason")
def open_unit_refuses(fw, line, reason):
    expect_reason(fw, line, reason)


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
