"""Watchdog (`hal::WatchDogStm`, the window watchdog): early warnings, feeding, resets and the warning period.

A started watchdog cannot be stopped, so every test resets the board afterwards. The warning period is
63 * 4096 * prescaler / PCLK1 for the smallest prescaler whose period is at least the timeout
(`expect.wwdg_warning_period`). `tests.watchdog.pin` is a bundle1 pin: the `pin=` toggle output is on its DIO, so the
logic analyzer measures the warning period.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import expect

pytestmark = pytest.mark.resets_board

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


@pytest.mark.slow
@pytest.mark.matrix("watchdog.behaviour")
def test_behaviour(fw, board_cfg, wdt_cfg, pclk1, timeout_ms, feed):
    """`feed=auto` keeps the board alive with one warning per period; with `feed=manual` the warning that is not
    answered is followed by a reset reported as `reset=wwdg`. The reset comes one counter tick after the warning,
    which at the shortest timeouts is too short to send the `EVT wdt` line: the line is only checked where the tick
    leaves time for it (`expect.wwdg_warning_outruns_reset`)."""
    index = wdt_cfg["index"]
    period = expect.wwdg_warning_period(timeout_ms, pclk1)
    boot_timeout = board_cfg.param("system.boot_timeout", 5.0)
    fw.wdt.start(index, timeout=timeout_ms, feed=feed)
    if feed == "manual":
        if expect.wwdg_warning_outruns_reset(timeout_ms, pclk1, board_cfg.terminal.baud, WARNING_LINE_MARGIN):
            warning = fw.wdt.wait_warning(index, timeout=period * 2 + 1)
            assert warning.as_int("warning") == 1
        boot = fw.system.wait_boot(timeout=period * 3 + boot_timeout)
        assert boot.reset == "wwdg"
        assert fw.system.info().reset == "wwdg"
        return
    window = observation(wdt_cfg, period)
    warnings = fw.terminal.collect_events("wdt", window)
    assert not fw.terminal.events("boot"), "the board reset although it was fed"
    assert warnings, "no early warning"
    assert all(event.as_int("index") == index for event in warnings)
    expected = window / period
    assert expected * 0.5 <= len(warnings) <= expected * 1.5 + 1, f"{len(warnings)} warnings in {window} s"
    fw.system.ping()


@pytest.mark.ad3
@pytest.mark.slow
@pytest.mark.matrix("watchdog.period")
def test_warning_period(fw, ad3, need, wdt_cfg, pclk1, timeout_ms):
    """The `pin=` output toggles on every early warning: the toggle interval is the warning period."""
    pin = wdt_cfg["pin"]
    dio = need.dio(pin)
    period = expect.wwdg_warning_period(timeout_ms, pclk1)
    periods = wdt_cfg["observe_periods"]
    duration = (periods + 1.5) * period
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / duration)
    capture = ad3.logic.arm(rate, int(duration * rate), trigger=(dio, "either"), pretrigger=0.02)
    fw.wdt.start(wdt_cfg["index"], timeout=timeout_ms, feed="auto", pin=pin)
    result = capture.wait(timeout=duration + period + 2.0)
    edges = [edge.index for edge in analysis.edges(result.channel(dio))]
    assert len(edges) >= periods, f"{len(edges)} toggles in {duration:.3f} s"
    intervals = [(b - a) / result.rate for a, b in zip(edges, edges[1:])]
    measured = statistics.median(intervals)
    assert measured == pytest.approx(period, rel=wdt_cfg["period_tolerance"], abs=2 / result.rate)
    assert not fw.terminal.events("boot"), "the board reset although it was fed"


def test_manual_feed(fw, board_cfg, wdt_cfg, pclk1):
    """`wdt.feed` faster than the warning period keeps the board alive; when it stops, the board resets."""
    index, timeout_ms = wdt_cfg["index"], wdt_cfg["manual_timeout_ms"]
    period = expect.wwdg_warning_period(timeout_ms, pclk1)
    fw.wdt.start(index, timeout=timeout_ms, feed="manual")
    end = time.monotonic() + wdt_cfg["feed_seconds"]
    while time.monotonic() < end:
        fw.wdt.feed(index)
        time.sleep(period / 4)
    assert not fw.terminal.events("boot"), "reset although fed"
    boot = fw.system.wait_boot(timeout=period * 3 + board_cfg.param("system.boot_timeout", 5.0))
    assert boot.reset == "wwdg"


def test_only_one_watchdog(fw, wdt_cfg):
    """A started watchdog cannot be stopped, so a second start is refused; its pin stays claimed."""
    pin = wdt_cfg["pin"]
    fw.wdt.start(wdt_cfg["index"], timeout=wdt_cfg["manual_timeout_ms"], pin=pin)
    with pytest.raises(FirmwareError) as error:
        fw.wdt.start(wdt_cfg["index"], timeout=wdt_cfg["manual_timeout_ms"])
    assert error.value.reason == "busy"
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(pin, "in")
    assert error.value.reason == "busy", "the toggle pin stays claimed until reset"


@pytest.mark.board_params("timeout_ms", "watchdog.open_timeouts_ms")
def test_start_timeouts(fw, wdt_cfg, pclk1, timeout_ms):
    """`timeout` is 1-30000 ms, and at most what the WWDG reaches at PCLK1 (`ERR range` beyond)."""
    fits = timeout_ms <= expect.WDT_TIMEOUT_MAX_MS and expect.wwdg_prescaler_for(timeout_ms, pclk1) is not None
    try:
        fw.wdt.start(wdt_cfg["index"], timeout=timeout_ms)
    except FirmwareError as error:
        assert error.reason == "range" and not fits, f"ERR {error.reason} for {timeout_ms} ms"
        return
    assert fits, f"{timeout_ms} ms accepted beyond {expect.wwdg_max_timeout_ms(pclk1)} ms"


def test_start_errors(fw, board_cfg, wdt_cfg, pclk1):
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
