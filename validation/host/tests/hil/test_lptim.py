"""Low-power timers (`hal::FreeRunningLowPowerTimerStm`, `hal::LowPowerTimerWithInterruptStm`) through the `lptim`
group: update rate on the marker pin for every prescaler, the WBA repetition counter, interrupt counts, the
free-running counter, stop, and the LPTIM shared with the LPTIM encoder (`qei lp=1`) and `lptpwm`.

Wiring set `bundle1`: `tests.lptim.marker` (gpio0) on a DIO; the marker toggles once per update interrupt, so it runs
at half the update rate `lptimclk / prescaler / (period + 1) / (rep + 1)`, with the LPTIM kernel clock `lptimclk` of
the board file's `clocks.lptim` (`lptim.open` reports it, test_reports_the_kernel_clock).

Scenarios: features/lptim.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.groups.timers import LPTIM_PERIOD_MAX, LPTIM_REPETITION_MAX, lptim_update_rate


@pytest.fixture
def lptim_cfg(board_cfg):
    return board_cfg.param("lptim")


def dispatchable(values):
    """Dispatched callbacks coalesce above 1 kHz: those rates run with `irq=immediate` only."""
    update = values.get("update")
    return values.get("irq") != "dispatched" or update is None or update.get("dispatched", True)


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def marker_frequency(ad3, dio, cfg, marker):
    periods = cfg["record_periods"]
    capture = ad3.logic.record_for(periods / marker, trigger=(dio, "rising"), timeout=periods / marker + 2)
    return capture.frequency(dio)


@pytest.mark.board_params("instance", "lptim.instances")
@scenario("lptim.feature", "Open reports the LPTIM kernel clock")
def test_reports_the_kernel_clock(instance):
    pass


@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.matrix("lptim.interrupt")
@pytest.mark.constraint(valid=dispatchable)
@scenario("lptim.feature", "The marker runs at half the update rate")
def test_update_marker(instance, irq, update):
    pass


@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.board_params("prescaler", "lptim.prescalers")
@scenario("lptim.feature", "Every prescaler divides the update rate")
def test_prescaler(instance, prescaler):
    pass


@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.board_params("rep", "lptim.repetitions")
@scenario("lptim.feature", "The repetition counter updates once every rep + 1 periods")
def test_repetition_counter(instance, rep):
    pass


@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.matrix("lptim.interrupt")
@pytest.mark.constraint(valid=dispatchable)
@scenario("lptim.feature", "The interrupt count follows the update rate")
def test_interrupt_count(instance, irq, update):
    pass


@pytest.mark.board_params("instance", "lptim.instances")
@scenario("lptim.feature", "The free-running counter counts without interrupts and stop freezes it")
def test_free_running_counter_and_stop(instance):
    pass


@pytest.mark.board_params("instance", "lptim.instances")
@scenario("lptim.feature", "Stop stops the marker")
def test_stop_stops_the_marker(instance):
    pass


@scenario("lptim.feature", "The repetition counter is there on the WBA only")
def test_repetition_support():
    pass


@scenario("lptim.feature", "Invalid open arguments are refused in the protocol order")
def test_open_errors():
    pass


@scenario("lptim.feature", "One LPTIM is open at a time")
def test_one_lptim_at_a_time():
    pass


@scenario("lptim.feature", "The marker pin is released on close")
def test_marker_is_released_on_close():
    pass


@pytest.mark.board_params("encoder", "qei.lp_instances")
@scenario("lptim.feature", "An LPTIM serves one group")
def test_lptim_shared_with_the_encoder(encoder):
    pass


@given("the marker pin is wired to a DIO", target_fixture="dio")
def marker_wired(need, lptim_cfg):
    return need.dio(lptim_cfg["marker"])


@given("the marker pin is not loaded by an option")
def marker_unloaded(need, lptim_cfg):
    need.unloaded(lptim_cfg["marker"])


@given("the first LPTIM under test", target_fixture="first")
def first_lptim(lptim_cfg):
    return lptim_cfg["instances"][0]["index"]


@given("the first two LPTIMs under test", target_fixture="pair")
def first_two_lptims(lptim_cfg):
    first, second = (instance["index"] for instance in lptim_cfg["instances"][:2])
    return first, second


@given("the LPTIM PWM on the LPTIM of the encoder in the board file, if any", target_fixture="lptpwm")
def lptpwm_of_encoder(board_cfg, encoder):
    return next((entry for entry in board_cfg.param("lptim_pwm.instances", []) if entry["index"] == encoder["index"]), None)


@when("the LPTIM opens without interrupts", target_fixture="lptimclk")
def open_without_interrupts(fw, instance):
    return fw.lptim.open(instance["index"], irq="none")


@when("the LPTIM opens with the update prescaler and period, the interrupt mode and the marker pin")
def open_update_with_marker(fw, lptim_cfg, instance, irq, update):
    fw.lptim.open(instance["index"], prescaler=update["prescaler"], period=update["period"], irq=irq, pin=lptim_cfg["marker"])


@when("the LPTIM opens with the prescaler, the prescaler period of the board file, immediate interrupts and the marker pin")
def open_prescaler_with_marker(fw, lptim_cfg, instance, prescaler):
    period = lptim_cfg["prescaler_period"]
    fw.lptim.open(instance["index"], prescaler=prescaler, period=period, irq="immediate", pin=lptim_cfg["marker"])


@when("the LPTIM opens with the repetition timing of the board file, the repetition count, immediate interrupts and the marker pin")
def open_repetition_with_marker(fw, lptim_cfg, instance, rep):
    timing = lptim_cfg["repetition_timing"]
    marker_pin = lptim_cfg["marker"]
    fw.lptim.open(instance["index"], prescaler=timing["prescaler"], period=timing["period"], rep=rep, irq="immediate", pin=marker_pin)


@when("the LPTIM opens with the update prescaler and period and the interrupt mode", target_fixture="lptimclk")
def open_update(fw, instance, irq, update):
    return fw.lptim.open(instance["index"], prescaler=update["prescaler"], period=update["period"], irq=irq)


@when("the LPTIM opens free-running without interrupts")
def open_free_running(fw, lptim_cfg, instance):
    free = lptim_cfg["free_running"]
    fw.lptim.open(instance["index"], prescaler=free["prescaler"], period=free["period"], irq="none")


@when("the LPTIM opens with the second interrupt update setting, immediate interrupts and the marker pin", target_fixture="lptimclk")
def open_second_update_with_marker(fw, lptim_cfg, instance):
    update = lptim_cfg["interrupt"]["update"][1]
    return fw.lptim.open(
        instance["index"], prescaler=update["prescaler"], period=update["period"], irq="immediate", pin=lptim_cfg["marker"]
    )


@when("the LPTIM starts")
def start(fw, instance):
    fw.lptim.start(instance["index"])


@when("the LPTIM starts twice")
def start_twice(fw, instance):
    fw.lptim.start(instance["index"])
    fw.lptim.start(instance["index"])


@when("the LPTIM stops")
def stop(fw, instance):
    fw.lptim.stop(instance["index"])


@when("the LPTIM stops twice")
def stop_twice(fw, instance):
    fw.lptim.stop(instance["index"])
    fw.lptim.stop(instance["index"])


@when("the counts are read over the counting window", target_fixture="counts")
def counts_over_window(fw, lptim_cfg, instance):
    index = instance["index"]
    start = time.monotonic()
    before = fw.lptim.count(index)
    time.sleep(lptim_cfg["window_s"])
    after = fw.lptim.count(index)
    elapsed = time.monotonic() - start
    return after, before, elapsed


@when("the counts are read", target_fixture="stopped")
def counts_read(fw, instance):
    return fw.lptim.count(instance["index"])


@when("the host waits the counting window")
def host_waits(lptim_cfg):
    time.sleep(lptim_cfg["window_s"])


@when("the first of them opens without interrupts")
def first_of_pair_opens(fw, pair):
    fw.lptim.open(pair[0], irq="none")


@when("the first LPTIM opens with immediate interrupts and the marker pin")
def first_opens_with_marker(fw, lptim_cfg, first):
    fw.lptim.open(first, irq="immediate", pin=lptim_cfg["marker"])


@when("the first LPTIM closes")
def first_closes(fw, first):
    fw.lptim.close(first)


@when("the LPTIM encoder opens")
def encoder_opens(fw, encoder):
    fw.qei.open(encoder["index"], lp=True, a=encoder["a"], b=encoder["b"])


@when("the LPTIM encoder closes")
def encoder_closes(fw, encoder):
    fw.qei.close(encoder["index"])


@when("the LPTIM of the encoder opens without interrupts")
def encoder_lptim_opens(fw, encoder):
    fw.lptim.open(encoder["index"], irq="none")


@when("the LPTIM of the encoder closes")
def encoder_lptim_closes(fw, encoder):
    fw.lptim.close(encoder["index"])


@when("the LPTIM PWM opens on it, if there is one")
def lptpwm_opens(fw, encoder, lptpwm):
    if lptpwm is not None:
        fw.lptpwm.open(encoder["index"], pins=lptpwm["pins"])


@then("it reports the LPTIM kernel clock of the board file")
def reports_kernel_clock(board_cfg, instance, lptimclk):
    assert lptimclk == board_cfg.clock("lptim", instance["index"])


@then("the marker runs at half the update rate of the LPTIM kernel clock within the frequency tolerance")
def marker_runs(ad3, board_cfg, lptim_cfg, instance, update, dio):
    lptimclk = board_cfg.clock("lptim", instance["index"])
    marker = lptim_update_rate(lptimclk, update["prescaler"], update["period"]) / 2
    assert marker_frequency(ad3, dio, lptim_cfg, marker) == pytest.approx(marker, rel=lptim_cfg["tolerance"]["frequency"])


@then("the marker runs at half the update rate of the prescaler and the prescaler period within the frequency tolerance")
def marker_runs_with_prescaler(ad3, board_cfg, lptim_cfg, instance, prescaler, dio):
    lptimclk = board_cfg.clock("lptim", instance["index"])
    marker = lptim_update_rate(lptimclk, prescaler, lptim_cfg["prescaler_period"]) / 2
    assert marker_frequency(ad3, dio, lptim_cfg, marker) == pytest.approx(marker, rel=lptim_cfg["tolerance"]["frequency"])


@then("the marker runs at half the update rate of the repetition timing and count within the frequency tolerance")
def marker_runs_with_repetition(ad3, board_cfg, lptim_cfg, instance, rep, dio):
    timing = lptim_cfg["repetition_timing"]
    lptimclk = board_cfg.clock("lptim", instance["index"])
    marker = lptim_update_rate(lptimclk, timing["prescaler"], timing["period"], rep) / 2
    assert marker_frequency(ad3, dio, lptim_cfg, marker) == pytest.approx(marker, rel=lptim_cfg["tolerance"]["frequency"])


@then("the interrupts counted match the update rate within the count tolerance and the interrupt latency")
def interrupts_match_rate(lptim_cfg, update, lptimclk, counts):
    rate = lptim_update_rate(lptimclk, update["prescaler"], update["period"])
    after, before, elapsed = counts
    tolerance = lptim_cfg["tolerance"]
    counted = after.irqs - before.irqs
    assert counted >= rate * elapsed * (1 - tolerance["count"]) - rate * tolerance["latency_s"], (counted, rate * elapsed)
    assert counted <= rate * elapsed * (1 + tolerance["count"]) + 1, (counted, rate * elapsed)


@then("the LPTIM has counted no interrupt")
def no_interrupt(fw, instance):
    assert fw.lptim.count(instance["index"]).irqs == 0


@then(parsers.parse("{readings:d} readings of the counter show no interrupt, stay within the period and are not all the same"))
def counter_counts(fw, lptim_cfg, instance, readings):
    free = lptim_cfg["free_running"]
    values = set()
    for _ in range(readings):
        reading = fw.lptim.count(instance["index"])
        assert reading.irqs == 0 and reading.cnt <= free["period"]
        values.add(reading.cnt)
    assert len(values) > 1, "the counter does not count"


@then("the counts read as before")
def counts_as_before(fw, instance, stopped):
    assert fw.lptim.count(instance["index"]) == stopped


@then("the marker stays still over the record periods at half the update rate")
def marker_still(ad3, lptim_cfg, dio, lptimclk):
    update = lptim_cfg["interrupt"]["update"][1]
    marker = lptim_update_rate(lptimclk, update["prescaler"], update["period"]) / 2
    capture = ad3.logic.record_for(lptim_cfg["record_periods"] / marker)
    assert len(set(capture.channel(dio))) == 1, "the marker moved after lptim.stop"


@then(parsers.parse('opening the first LPTIM with a repetition count past the largest fails with "{reason}"'))
def repetition_past_largest_refused(fw, first, reason):
    expect_error(reason, fw.lptim.open, first, rep=LPTIM_REPETITION_MAX + 1)


@then(
    parsers.parse(
        'opening the first LPTIM with a repetition count of {count:d} fails with "{reason}", if the board file says it has no '
        "repetition counter"
    )
)
def repetition_unsupported(fw, lptim_cfg, first, count, reason):
    if not lptim_cfg["repetition"]:
        expect_error(reason, fw.lptim.open, first, rep=count)


@then("the first LPTIM opens with the largest repetition count and closes, if the board file says it has a repetition counter")
def largest_repetition_opens(fw, lptim_cfg, first):
    if lptim_cfg["repetition"]:
        fw.lptim.open(first, rep=LPTIM_REPETITION_MAX)
        fw.lptim.close(first)


@then("every invalid open of the first LPTIM is refused with its reason in the protocol order")
def open_errors(fw, board_cfg, lptim_cfg, first):
    marker = lptim_cfg["marker"]
    cases = [
        ({"irq": "nmi"}, "usage"),
        ({"irq": "none", "pin": marker}, "usage"),
        ({"period": 0}, "range"),
        ({"period": LPTIM_PERIOD_MAX + 1}, "range"),
        ({"prescaler": 0}, "range"),
        ({"prescaler": 3}, "range"),
        ({"prescaler": 256}, "range"),
        ({"pin": board_cfg.param("system.unbonded_pins")[0]}, "pin"),
        ({"period": 1}, "range"),
        ({"period": 1, "irq": "immediate"}, "range"),
    ]
    for options, reason in cases:
        expect_error(reason, fw.lptim.open, first, **options)


@then(parsers.parse("the first LPTIM opens with period {period:d} without interrupts and closes"))
def fastest_opens_without_interrupts(fw, first, period):
    assert fw.lptim.open(first, period=period, irq="none") > 0, "the update interrupt rate limit needs an interrupt"
    fw.lptim.close(first)


@then(parsers.parse('opening each missing LPTIM of the board file fails with "{reason}"'))
def missing_refused(fw, lptim_cfg, reason):
    for missing in lptim_cfg["missing"]:
        expect_error(reason, fw.lptim.open, missing)


@then(parsers.parse('starting, stopping, reading and closing the closed first LPTIM fail with "{reason}"'))
def closed_commands_refused(fw, first, reason):
    for command in ("start", "stop", "count", "close"):
        expect_error(reason, getattr(fw.lptim, command), first)


@then(parsers.parse('opening the first LPTIM counting down fails with "{reason}"'))
def counting_down_refused(fw, first, reason):
    assert fw.terminal.command(f"lptim.open {first} mode=down", check=False).reason == reason


@then(parsers.parse('opening the first LPTIM with an unknown pin alias fails with "{reason}"'))
def unknown_alias_refused(fw, first, reason):
    assert fw.terminal.command(f"lptim.open {first} pin=nosuchalias", check=False).reason == reason


@then(parsers.parse('opening the second of them without interrupts fails with "{reason}"'))
def second_of_pair_refused(fw, pair, reason):
    expect_error(reason, fw.lptim.open, pair[1], irq="none")


@then(parsers.parse('configuring the marker pin as an input fails with "{reason}"'))
def marker_input_refused(fw, lptim_cfg, reason):
    expect_error(reason, fw.gpio.cfg, lptim_cfg["marker"], "in")


@then("the marker pin can be configured as an input")
def marker_input(fw, lptim_cfg):
    fw.gpio.cfg(lptim_cfg["marker"], "in")


@then(parsers.parse('opening the LPTIM of the encoder without interrupts fails with "{reason}"'))
def encoder_lptim_refused(fw, encoder, reason):
    expect_error(reason, fw.lptim.open, encoder["index"], irq="none")


@then(parsers.parse('opening the LPTIM PWM on it fails with "{reason}", if there is one'))
def lptpwm_refused(fw, encoder, lptpwm, reason):
    if lptpwm is not None:
        expect_error(reason, fw.lptpwm.open, encoder["index"], pins=lptpwm["pins"])


@then(parsers.parse('opening the LPTIM encoder fails with "{reason}"'))
def encoder_refused(fw, encoder, reason):
    expect_error(reason, fw.qei.open, encoder["index"], lp=True, a=encoder["a"], b=encoder["b"])


@then(parsers.parse('opening the LPTIM of the encoder without interrupts fails with "{reason}", if there is an LPTIM PWM'))
def encoder_lptim_refused_by_lptpwm(fw, encoder, lptpwm, reason):
    if lptpwm is not None:
        expect_error(reason, fw.lptim.open, encoder["index"], irq="none")
