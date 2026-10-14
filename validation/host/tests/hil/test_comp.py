"""Analog comparators: EMIL's `comp` commands over `hal::ComparatorStm` and `hal::SynchronousComparatorStm`, and
`comp.dac`: the output against the internal thresholds, hysteresis, polarity, edge interrupts, the output pin, the
propagation delay and blanking. bundle1 wiring (COMP1 INP on W1, outputs and pulsed inputs on DIOs).

Scenarios: features/comp.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.groups.comp import DAC_FULL_SCALE, threshold_volts


@pytest.fixture
def comp_cfg(board_cfg):
    return board_cfg.param("comp")


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def settle(comp_cfg):
    time.sleep(comp_cfg["settle_s"])


@pytest.mark.board_params("threshold", "comp.thresholds")
@scenario("comp.feature", "The output follows W1 against the internal threshold")
def test_follows(threshold):
    pass


@pytest.mark.board_params("threshold", "comp.thresholds")
@scenario("comp.feature", "The inverted output follows W1 the other way round")
def test_inverted(threshold):
    pass


@scenario("comp.feature", "The DAC sets the threshold")
def test_dac_threshold():
    pass


@scenario("comp.feature", "Hysteresis widens the window between the switching levels")
def test_hysteresis():
    pass


@pytest.mark.board_params("irq, frequency, pulses", "comp.edges.cases")
@pytest.mark.board_params("edge", "comp.edges.kinds")
@scenario("comp.feature", "Edges of the comparator output are counted")
def test_edges(edge, irq, frequency, pulses):
    pass


@scenario("comp.feature", "The output follows the input within the propagation delay")
def test_delay():
    pass


@scenario("comp.feature", "Blanking masks the output while the timer compare output is high")
def test_blanking():
    pass


@scenario("comp.feature", "The synchronous comparator reads but refuses edge interrupts")
def test_synchronous():
    pass


@scenario("comp.feature", "The DAC of a comparator is set and owned while it is open")
def test_dac_ownership():
    pass


@scenario("comp.feature", "A pin held by another group cannot be opened")
def test_pin_held():
    pass


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("comp.open", "usage"),
        ("comp.open 1 bogus=1", "usage"),
        ("comp.open 8", "range"),
        ("comp.open 0", "range"),
        ("comp.open 1 inp=PB5", "pin"),
        ("comp.open 1 inm=PB5", "pin"),
        ("comp.open 1 inm=bogus", "pin"),
        ("comp.open 1 dac=100", "usage"),
        ("comp.open 1 inm=dac dac=4096", "range"),
        ("comp.open 1 hyst=15", "range"),
        ("comp.open 1 hyst=80", "range"),
        ("comp.open 1 pol=x", "usage"),
        ("comp.open 1 out=PB5", "pin"),
        ("comp.open 1 blank=tim9oc1", "usage"),
        ("comp.open 1 blank=tim2oc4", "unsupported"),
        ("comp.open 1 irq=x", "usage"),
        ("comp.read 1", "notopen"),
        ("comp.read 8", "range"),
        ("comp.irq 1", "usage"),
        ("comp.irq 1 up", "notopen"),
        ("comp.count 1", "notopen"),
        ("comp.close 1", "notopen"),
        ("comp.dac 1", "usage"),
        ("comp.dac 1 4096", "range"),
        ("comp.dac 8 0", "range"),
        ("comp.dac 1 0", "notopen"),
    ],
)
@scenario("comp.feature", "Malformed and out-of-range commands are refused")
def test_errors(line, reason):
    pass


@given("comparator 1 is open with the threshold and its output pin", target_fixture="follow")
def open_follow(fw, need, comp_cfg, threshold):
    follow = comp_cfg["follow"]
    state = {"w": need.wavegen(follow["inp"]), "dio": need.dio(follow["out"]), "level": threshold_volts(threshold)}
    fw.comp.open(follow["index"], inp=follow["inp"], inm=threshold, out=follow["out"])
    return state


@given("comparator 1 is open with the threshold, inverted, with its output pin", target_fixture="follow")
def open_inverted(fw, need, comp_cfg, threshold):
    follow = comp_cfg["follow"]
    state = {"w": need.wavegen(follow["inp"]), "dio": need.dio(follow["out"]), "level": threshold_volts(threshold)}
    fw.comp.open(follow["index"], inp=follow["inp"], inm=threshold, out=follow["out"], pol="inverted")
    return state


@when("W1 is set below the threshold by the margin")
def below(ad3, comp_cfg, follow):
    ad3.wavegen.dc(follow["w"], max(0.0, follow["level"] - comp_cfg["margin_v"]))
    settle(comp_cfg)


@when("W1 is set above the threshold by the margin")
def above(ad3, comp_cfg, follow):
    ad3.wavegen.dc(follow["w"], follow["level"] + comp_cfg["margin_v"])
    settle(comp_cfg)


@then(parsers.parse("the comparator and its output pin read {value:d}"))
def reads(fw, ad3, comp_cfg, follow, value):
    assert fw.comp.read(comp_cfg["follow"]["index"]) == value
    assert ad3.dio.read(follow["dio"]) == value


@given("comparator 1 is open against the internal DAC with W1 at the DAC level", target_fixture="dac_case")
def open_dac(fw, ad3, need, comp_cfg):
    follow, dac = comp_cfg["follow"], comp_cfg["dac"]
    ad3.wavegen.dc(need.wavegen(follow["inp"]), dac["level_v"])
    fw.comp.open(follow["index"], inp=follow["inp"], inm="dac")
    settle(comp_cfg)
    return dac


@when("the firmware searches the DAC code where the output changes", target_fixture="trip_code")
def search_code(fw, comp_cfg):
    index = comp_cfg["follow"]["index"]
    low, high = 0, DAC_FULL_SCALE
    while high - low > 1:
        middle = (low + high) // 2
        fw.comp.dac(index, middle)
        settle(comp_cfg)
        if fw.comp.read(index):
            low = middle
        else:
            high = middle
    return high


@then("the code is the W1 level in DAC codes within the tolerance")
def code_matches(trip_code, dac_case, comp_cfg):
    expected = dac_case["level_v"] / comp_cfg["vref"] * DAC_FULL_SCALE
    assert trip_code == pytest.approx(expected, abs=dac_case["tolerance_codes"])


def switching_levels(fw, ad3, comp_cfg, w, index):
    hysteresis = comp_cfg["hysteresis"]
    centre = threshold_volts("vref/2")
    steps = int(2 * hysteresis["span_v"] / hysteresis["step_v"]) + 1
    levels = [centre - hysteresis["span_v"] + i * hysteresis["step_v"] for i in range(steps)]
    rising = falling = None
    ad3.wavegen.dc(w, levels[0])
    settle(comp_cfg)
    for level in levels:
        ad3.wavegen.dc(w, level)
        settle(comp_cfg)
        if fw.comp.read(index):
            rising = level
            break
    for level in reversed(levels):
        ad3.wavegen.dc(w, level)
        settle(comp_cfg)
        if not fw.comp.read(index):
            falling = level
            break
    assert rising is not None and falling is not None, "the output did not switch within the ramp"
    return rising, falling


@given("W1 ramps across the half VREFINT threshold of comparator 1 at every hysteresis setting", target_fixture="windows")
def ramp_windows(fw, ad3, need, comp_cfg):
    follow = comp_cfg["follow"]
    w = need.wavegen(follow["inp"])
    windows = {}
    for setting in comp_cfg["hysteresis"]["settings"]:
        fw.comp.open(follow["index"], inp=follow["inp"], inm="vref/2", hyst=setting)
        rising, falling = switching_levels(fw, ad3, comp_cfg, w, follow["index"])
        fw.comp.close(follow["index"])
        windows[setting] = rising - falling
    return windows


@then("the window between the rising and the falling switching level grows with the setting")
def window_grows(windows, comp_cfg):
    step = comp_cfg["hysteresis"]["step_v"]
    settings = sorted(windows)
    for lower, higher in zip(settings, settings[1:], strict=False):
        assert windows[higher] >= windows[lower] - step, windows


@then("every window is within the limits of its setting")
def window_limits(windows, comp_cfg):
    hysteresis = comp_cfg["hysteresis"]
    for setting, window in windows.items():
        assert window >= hysteresis["min_fraction"] * setting / 1000 - hysteresis["step_v"], (setting, window)
        assert window <= 2 * setting / 1000 + hysteresis["extra_v"], (setting, window)


@given("comparator 2 is open with its edge interrupt and the handler type", target_fixture="edge_dio")
def open_edges(fw, need, comp_cfg, edge, irq):
    edges = comp_cfg["edges"]
    dio = need.dio(edges["inp"])
    fw.comp.open(edges["index"], inp=edges["inp"], inm="vref/2", hyst=edges["hyst"], irq=irq)
    fw.comp.irq(edges["index"], edge)
    fw.comp.count(edges["index"], clear=True)
    return dio


@when("the AD3 sends the pulses on the INP DIO at the frequency")
def send_pulses(ad3, edge_dio, pulses, frequency):
    ad3.pattern.pulses(edge_dio, pulses, frequency)
    ad3.pattern.wait_done(timeout=pulses / frequency + 2)
    time.sleep(0.05)


@then("the edge count is the number of edges of that kind")
def edges_counted(fw, comp_cfg, edge, pulses):
    expected = 2 * pulses if edge == "both" else pulses
    assert fw.comp.count(comp_cfg["edges"]["index"]) == expected


@given("comparator 4 is open with its output pin", target_fixture="delay_dios")
def open_delay(fw, need, comp_cfg):
    delay = comp_cfg["delay"]
    dios = need.dio(delay["inp"]), need.dio(delay["out"])
    fw.comp.open(delay["index"], inp=delay["inp"], inm="vref/2", out=delay["out"])
    return dios


@when("the logic analyzer records the INP and the output DIO while the AD3 pulses the INP", target_fixture="delay_capture")
def record_delay(ad3, comp_cfg, delay_dios):
    delay = comp_cfg["delay"]
    duration = delay["pulses"] / delay["frequency"] * 1.2
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / duration)
    pending = ad3.logic.arm(rate, int(duration * rate), trigger=(delay_dios[0], "rising"), pretrigger=0.05)
    ad3.pattern.pulses(delay_dios[0], delay["pulses"], delay["frequency"])
    return pending.wait(timeout=duration + 2)


@then("every output edge follows its input edge within the maximum delay")
def within_delay(delay_capture, delay_dios, comp_cfg):
    rate = delay_capture.rate
    inputs, outputs = delay_capture.channel(delay_dios[0]), delay_capture.channel(delay_dios[1])
    for times in (analysis.rising_times, analysis.falling_times):
        sent, seen = times(inputs, rate), times(outputs, rate)
        assert sent and len(seen) == len(sent), (len(sent), len(seen))
        delays = [after - before for before, after in zip(sent, seen, strict=True)]
        assert all(0 <= value <= comp_cfg["delay"]["max_s"] for value in delays), delays


@given("comparator 1 is open blanked by the TIM2 CH3 compare output with W1 above the threshold", target_fixture="blanking_dios")
def open_blanked(fw, ad3, need, comp_cfg):
    follow, blanking = comp_cfg["follow"], comp_cfg["blanking"]
    dios = need.dio(blanking["pin"]), need.dio(follow["out"])
    ad3.wavegen.dc(need.wavegen(follow["inp"]), blanking["level_v"])
    fw.comp.open(follow["index"], inp=follow["inp"], inm="vref/2", out=follow["out"], blank=blanking["source"])
    return dios


@when("TIM2 CH3 runs at the blanking frequency and duty", target_fixture="blanking_capture")
def run_blanking(fw, ad3, comp_cfg, blanking_dios):
    blanking = comp_cfg["blanking"]
    fw.pwm.open(blanking["timer"], channels=[blanking["channel"]], pins=[blanking["pin"]], freq=blanking["frequency"])
    fw.pwm.duty(blanking["timer"], blanking["duty"])
    periods = 10
    return ad3.logic.record_for(
        periods / blanking["frequency"], trigger=(blanking_dios[0], "rising"), timeout=periods / blanking["frequency"] + 2
    )


@then("the output pin is low while TIM2 CH3 is high and high while it is low")
def blanked(blanking_capture, blanking_dios, comp_cfg):
    rate = blanking_capture.rate
    timer, output = blanking_capture.channel(blanking_dios[0]), blanking_capture.channel(blanking_dios[1])
    guard = int(comp_cfg["blanking"]["edge_s"] * rate) + 1
    changes = [index for index in range(1, len(timer)) if timer[index] != timer[index - 1]]
    checked = 0
    for index, (blanking, level) in enumerate(zip(timer, output, strict=True)):
        if any(abs(index - change) <= guard for change in changes):
            continue
        assert level == 1 - blanking, f"sample {index}: TIM2 CH3 {blanking}, output {level}"
        checked += 1
    assert checked > len(timer) // 2


@when("comparator 1 is open as a synchronous comparator")
def open_synchronous(fw, comp_cfg):
    fw.comp.open(comp_cfg["follow"]["index"], sync=True)


@then("comp.read answers its output")
def read_answers(fw, comp_cfg):
    assert fw.comp.read(comp_cfg["follow"]["index"]) in (0, 1)


@then("comp.irq answers unsupported")
def irq_unsupported(fw, comp_cfg):
    expect_reason(fw, f"comp.irq {comp_cfg['follow']['index']} rising", "unsupported")


@when("comparator 1 is open against the internal DAC")
def open_internal_dac(fw, comp_cfg):
    fw.comp.open(comp_cfg["follow"]["index"], inm="dac")


@then("comp.dac sets the code")
def dac_sets(fw, comp_cfg):
    assert fw.comp.dac(comp_cfg["follow"]["index"], 1000) == 1000


@then("comp.dac of another comparator answers notopen")
def other_dac(fw):
    expect_reason(fw, "comp.dac 2 1000", "notopen")


@when("comparator 1 is closed")
def close_comparator(fw, comp_cfg):
    fw.comp.close(comp_cfg["follow"]["index"])


@then("comp.dac answers notopen")
def dac_notopen(fw, comp_cfg):
    expect_reason(fw, f"comp.dac {comp_cfg['follow']['index']} 1000", "notopen")


@given("the INP pin of comparator 1 is held by the gpio group")
def gpio_holds(fw, comp_cfg):
    fw.gpio.cfg(comp_cfg["follow"]["inp"], "in")


@then("comp.open of comparator 1 answers busy")
def open_busy(fw, comp_cfg):
    expect_reason(fw, f"comp.open {comp_cfg['follow']['index']}", "busy")


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
