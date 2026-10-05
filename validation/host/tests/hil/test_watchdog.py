"""Watchdog (`hal::WatchDogStm`, the window watchdog): early warnings, feeding, resets and the warning period.

A started watchdog cannot be stopped, so every test resets the board afterwards. The warning period is
63 * 4096 * prescaler / PCLK1 for the smallest prescaler whose period is at least the timeout
(`expect.wwdg_warning_period`). `tests.watchdog.pin` is a bundle1 pin: the `pin=` toggle output is on its DIO, so the
logic analyzer measures the warning period.

Scenarios: features/watchdog.feature.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect

# The counter tick between an unanswered warning and the reset must outlast the `EVT wdt` line this many times, which
# leaves room for the event loop's latency before the line starts.
WARNING_LINE_MARGIN = 2


@pytest.fixture
def wdt_cfg(board_cfg):
    return board_cfg.param("watchdog")


@pytest.fixture
def pclk1(board_cfg):
    return board_cfg.clock("pclk1")


@pytest.fixture(autouse=True)
def reset_afterwards(fw, board_cfg):
    yield
    boot_timeout = board_cfg.param("system.boot_timeout", 5.0)
    fw.terminal.drain_events()
    try:
        fw.system.ping()
        fw.system.reset(timeout=boot_timeout)
    except Exception:  # noqa: BLE001 - the board may be rebooting right now
        fw.system.wait_boot(timeout=boot_timeout)
    fw.terminal.drain_events()


def observation(wdt_cfg, period):
    return max(0.3, wdt_cfg["observe_periods"] * period)


@pytest.mark.matrix("watchdog.behaviour")
@scenario("watchdog.feature", "Fed, the watchdog warns once per period; unfed, it resets the board")
def test_behaviour(timeout_ms, feed):
    pass


@pytest.mark.matrix("watchdog.period")
@scenario("watchdog.feature", "The pin toggles once per warning period")
def test_warning_period(timeout_ms):
    pass


@scenario("watchdog.feature", "Manual feeding keeps the board alive until it stops")
def test_manual_feed():
    pass


@scenario("watchdog.feature", "Only one watchdog starts")
def test_only_one_watchdog():
    pass


@pytest.mark.board_params("timeout_ms", "watchdog.open_timeouts_ms")
@scenario("watchdog.feature", "The timeout is refused beyond the reach of the WWDG")
def test_start_timeouts(timeout_ms):
    pass


@scenario("watchdog.feature", "Invalid start arguments are refused")
def test_start_errors():
    pass


@given("the watchdog pin is wired to a DIO", target_fixture="dio")
def pin_wired(need, wdt_cfg):
    return need.dio(wdt_cfg["pin"])


@when("the watchdog starts with the timeout and the feed mode")
def start_with_feed(fw, wdt_cfg, timeout_ms, feed):
    fw.wdt.start(wdt_cfg["index"], timeout=timeout_ms, feed=feed)


@when("the logic analyzer arms on either edge of the pin for the observed warning periods", target_fixture="capture")
def arm_on_pin(ad3, wdt_cfg, pclk1, timeout_ms, dio):
    period = expect.wwdg_warning_period(timeout_ms, pclk1)
    duration = (wdt_cfg["observe_periods"] + 1.5) * period
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / duration)
    return ad3.logic.arm(rate, int(duration * rate), trigger=(dio, "either"), pretrigger=0.02)


@when("the watchdog starts with the timeout, automatic feed and the pin")
def start_with_pin(fw, wdt_cfg, timeout_ms):
    fw.wdt.start(wdt_cfg["index"], timeout=timeout_ms, feed="auto", pin=wdt_cfg["pin"])


@when("the watchdog starts with the manual timeout and manual feed")
def start_manual(fw, wdt_cfg):
    fw.wdt.start(wdt_cfg["index"], timeout=wdt_cfg["manual_timeout_ms"], feed="manual")


@when("the host feeds it every quarter warning period for the feed time")
def feed_for_a_while(fw, wdt_cfg, pclk1):
    period = expect.wwdg_warning_period(wdt_cfg["manual_timeout_ms"], pclk1)
    end = time.monotonic() + wdt_cfg["feed_seconds"]
    while time.monotonic() < end:
        fw.wdt.feed(wdt_cfg["index"])
        time.sleep(period / 4)


@when("the watchdog starts with the manual timeout and the pin")
def start_manual_with_pin(fw, wdt_cfg):
    fw.wdt.start(wdt_cfg["index"], timeout=wdt_cfg["manual_timeout_ms"], pin=wdt_cfg["pin"])


@then(
    parsers.parse(
        'if the feed is manual, the board warns once where the warning line outruns the reset, then resets with reason "{reason}", '
        "which info reports too"
    )
)
def unfed_resets(fw, board_cfg, wdt_cfg, pclk1, timeout_ms, feed, reason):
    if feed != "manual":
        return
    index = wdt_cfg["index"]
    period = expect.wwdg_warning_period(timeout_ms, pclk1)
    boot_timeout = board_cfg.param("system.boot_timeout", 5.0)
    if expect.wwdg_warning_outruns_reset(timeout_ms, pclk1, board_cfg.terminal.baud, WARNING_LINE_MARGIN):
        warning = fw.wdt.wait_warning(index, timeout=period * 2 + 1)
        assert warning.as_int("warning") == 1
    boot = fw.system.wait_boot(timeout=period * 3 + boot_timeout)
    assert boot.reset == reason
    assert fw.system.info().reset == reason


@then(
    "if the feed is not manual, the watchdog warns about once per warning period over the observation window without a reset, "
    "and the board still answers ping"
)
def fed_warns(fw, wdt_cfg, pclk1, timeout_ms, feed):
    if feed == "manual":
        return
    index = wdt_cfg["index"]
    period = expect.wwdg_warning_period(timeout_ms, pclk1)
    window = observation(wdt_cfg, period)
    warnings = fw.terminal.collect_events("wdt", window)
    assert not fw.terminal.events("boot"), "the board reset although it was fed"
    assert warnings, "no early warning"
    assert all(event.as_int("index") == index for event in warnings)
    expected = window / period
    assert expected * 0.5 <= len(warnings) <= expected * 1.5 + 1, f"{len(warnings)} warnings in {window} s"
    fw.system.ping()


@then("the pin toggles at least once per observed warning period, at the warning period within the period tolerance")
def pin_toggles(wdt_cfg, pclk1, timeout_ms, dio, capture):
    period = expect.wwdg_warning_period(timeout_ms, pclk1)
    periods = wdt_cfg["observe_periods"]
    duration = (periods + 1.5) * period
    result = capture.wait(timeout=duration + period + 2.0)
    edges = [edge.index for edge in analysis.edges(result.channel(dio))]
    assert len(edges) >= periods, f"{len(edges)} toggles in {duration:.3f} s"
    intervals = [(b - a) / result.rate for a, b in zip(edges, edges[1:])]
    measured = statistics.median(intervals)
    assert measured == pytest.approx(period, rel=wdt_cfg["period_tolerance"], abs=2 / result.rate)


@then("the board did not reset although it was fed")
def fed_board_did_not_reset(fw):
    assert not fw.terminal.events("boot"), "the board reset although it was fed"


@then("the board did not reset while it was fed")
def board_did_not_reset_while_fed(fw):
    assert not fw.terminal.events("boot"), "reset although fed"


@then(parsers.parse('the board resets with reason "{reason}" within three warning periods and the boot timeout'))
def unfed_board_resets(fw, board_cfg, wdt_cfg, pclk1, reason):
    period = expect.wwdg_warning_period(wdt_cfg["manual_timeout_ms"], pclk1)
    boot = fw.system.wait_boot(timeout=period * 3 + board_cfg.param("system.boot_timeout", 5.0))
    assert boot.reset == reason


@then(parsers.parse('starting it again with the manual timeout fails with "{reason}"'))
def second_start_refused(fw, wdt_cfg, reason):
    with pytest.raises(FirmwareError) as error:
        fw.wdt.start(wdt_cfg["index"], timeout=wdt_cfg["manual_timeout_ms"])
    assert error.value.reason == reason


@then(parsers.parse('configuring the pin as an input fails with "{reason}"'))
def pin_claimed(fw, wdt_cfg, reason):
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(wdt_cfg["pin"], "in")
    assert error.value.reason == reason, "the toggle pin stays claimed until reset"


@then(
    'starting the watchdog with the timeout fails with "range" exactly when the timeout is past the largest one or out of reach of '
    "the WWDG at PCLK1"
)
def start_timeout(fw, wdt_cfg, pclk1, timeout_ms):
    fits = timeout_ms <= expect.WDT_TIMEOUT_MAX_MS and expect.wwdg_prescaler_for(timeout_ms, pclk1) is not None
    try:
        fw.wdt.start(wdt_cfg["index"], timeout=timeout_ms)
    except FirmwareError as error:
        assert error.reason == "range" and not fits, f"ERR {error.reason} for {timeout_ms} ms"
        return
    assert fits, f"{timeout_ms} ms accepted beyond {expect.wwdg_max_timeout_ms(pclk1)} ms"


@then("every invalid start is refused with its reason")
def start_errors(fw, board_cfg, wdt_cfg, pclk1):
    index = wdt_cfg["index"]
    cases = [
        ({"timeout": 0}, "range"),
        ({"timeout": expect.WDT_TIMEOUT_MAX_MS + 1}, "range"),
        ({"timeout": expect.wwdg_max_timeout_ms(pclk1) + 1, "pin": board_cfg.terminal.pins[0]}, "range"),
        ({"timeout": 100, "feed": "sometimes"}, "usage"),
        ({"timeout": 100, "pin": board_cfg.param("system.unbonded_pins")[0]}, "pin"),
        ({"timeout": 100, "pin": board_cfg.terminal.pins[0]}, "busy"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.wdt.start(index, **options)
        assert error.value.reason == reason, options


@then("the malformed command lines are refused with their reason")
def malformed_lines_refused(fw, wdt_cfg):
    index = wdt_cfg["index"]
    for line, reason in (
        (f"wdt.start {index}", "usage"),
        (f"wdt.start {index} timeout=100 reset=0", "usage"),
        (f"wdt.start {index + 1} timeout=100", "range"),
        (f"wdt.feed {index}", "notopen"),
        (f"wdt.feed {index + 1}", "range"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == reason, line
