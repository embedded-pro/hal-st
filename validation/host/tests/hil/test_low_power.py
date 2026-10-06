"""Low-power mode through the `lpm` group: `hal::LowPowerModeStm::Enter` inside a window where only the wake line's
EXTI interrupt and the scaffold timer (TIM17, 1 us ticks, also the safety timeout) are enabled and the SysTick tick is
off. The marker pin is low while the core sleeps; `sleeps` (the `Enter` calls, about one per millisecond when WFI
really sleeps) shows that it did.

`deep` maps to Sleep on STM32WB/WBA (`LowPowerModeStm::Stop`), so it behaves like `sleep` and never calls the
clock-restore callback (`restored=0`). The wake edge comes from the AD3 on the wake pin (bundle1 `gpio0`); the logic
analyzer sees the marker low before the edge and high right after it.

Scenarios: features/low_power.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.firmware import settle


@pytest.fixture
def lp_cfg(board_cfg):
    return board_cfg.param("lowpower")


def expect_reason(fw, line: str, reason: str) -> None:
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


@pytest.mark.board_params("mode", "lowpower.modes")
@pytest.mark.board_params("edge", "lowpower.edges")
@scenario("low_power.feature", "The core sleeps until the edge on the wake pin")
def test_wake_on_edge(mode, edge):
    pass


@scenario("low_power.feature", "Without an edge the scaffold timer ends the window")
def test_timeout():
    pass


@scenario("low_power.feature", "Entering low-power mode releases the pins")
def test_pins_released():
    pass


@scenario("low_power.feature", "Malformed, out-of-range and conflicting commands are refused")
def test_errors():
    pass


@given("the run does not use the fake AD3")
def not_fake_ad3(request):
    if request.config.getoption("--fake"):
        pytest.skip("the fake AD3 sees no board")


@given("the run does not use the fake firmware")
def not_fake_firmware(request):
    if request.config.getoption("--fake"):
        pytest.skip("the fake firmware has no wake pin model: it wakes at once")


@given("the wake pin and the marker pin are wired to DIOs", target_fixture="lp_dios")
def wake_and_marker_wired(need, lp_cfg):
    wake_dio, marker_dio = need.dio(lp_cfg["wake"]), need.dio(lp_cfg["marker"])
    return wake_dio, marker_dio


@given("no enabled option loads the wake pin or the marker pin")
def pins_unloaded(need, lp_cfg):
    need.unloaded(lp_cfg["wake"], lp_cfg["marker"])


@when(
    "the AD3 drives the wake DIO idle for the edge, the logic analyzer arms on the edge, the core enters the mode until the edge on the "
    "wake pin with the marker and the wake timeout, and after the wake delay the AD3 drives the edge; the wake DIO is released even if "
    "that fails",
    target_fixture="woken",
)
def wake_on_edge(fw, ad3, lp_cfg, lp_dios, mode, edge):
    wake_dio, _ = lp_dios
    idle, active = (0, 1) if edge == "rising" else (1, 0)
    delay = lp_cfg["wake_delay_s"]
    rate = min(ad3.logic.clock_hz, lp_cfg["la_rate"])
    samples = ad3.logic.buffer_size
    ad3.dio.drive(wake_dio, idle)
    capture = ad3.logic.arm(rate, samples, trigger=(wake_dio, edge), pretrigger=0.5)
    timeout_ms = lp_cfg["wake_timeout_ms"]
    pending = fw.lpm.begin_enter(mode, wake=lp_cfg["wake"], edge=edge, marker=lp_cfg["marker"], timeout=timeout_ms)
    try:
        time.sleep(delay)
        ad3.dio.drive(wake_dio, active)
        result = capture.wait(timeout=2.0)
        wake = pending.wait()
    finally:
        settle(pending.pending, timeout_ms / 1000 + 1)
        ad3.dio.release(wake_dio)
    return {"wake": wake, "result": result}


@when(parsers.parse('the core enters sleep with the wake pin and the marker pin for the timeout, unless that fails with "{reason}"'))
def enter_sleep(fw, lp_cfg, reason):
    try:
        fw.lpm.enter("sleep", wake=lp_cfg["wake"], marker=lp_cfg["marker"], timeout=lp_cfg["timeout_ms"])
    except FirmwareError as error:
        assert error.reason == reason


@then("the core woke by EXTI without restoring the clock")
def woke_by_exti(woken):
    wake = woken["wake"]
    assert (wake.woke, wake.restored) == ("exti", 0), wake.raw


@then("it slept at least half the wake delay and at most the wake timeout")
def time_asleep(woken, lp_cfg):
    wake, delay, timeout_ms = woken["wake"], lp_cfg["wake_delay_s"], lp_cfg["wake_timeout_ms"]
    assert delay * 1e6 * 0.5 <= wake.us <= timeout_ms * 1000, wake.raw


@then("it returned from Enter at least once and at most twice per millisecond asleep plus twice")
def sleeps(woken):
    wake = woken["wake"]
    assert 1 <= wake.sleeps <= 2 * (wake.us // 1000) + 2, wake.raw


@then("the marker was low before the wake edge", target_fixture="marker")
def marker_low_before_edge(woken, lp_dios):
    _, marker_dio = lp_dios
    result = woken["result"]
    marker = result.channel(marker_dio)
    trigger = result.trigger_index
    assert trigger is not None
    assert all(bit == 0 for bit in marker[:trigger]), "the marker was high before the wake edge"
    return marker


@then("the marker rose after the wake edge within the latency")
def marker_rose(woken, lp_cfg, marker):
    result = woken["result"]
    trigger = result.trigger_index
    rises = [change.index for change in analysis.edges(marker) if change.index >= trigger and change.rising]
    assert rises, "the marker did not rise after the wake edge"
    latency = (rises[0] - trigger) / result.rate
    assert latency <= lp_cfg["latency_us"] * 1e-6, f"woke {latency * 1e6:.1f} us after the edge"


@then(
    parsers.parse(
        'entering sleep with the wake pin and the marker pin for the timeout fails with "{reason}" '
        "after at least {percent:d} % of the timeout"
    )
)
def timeout_ends_window(fw, lp_cfg, reason, percent):
    timeout_ms = lp_cfg["timeout_ms"]
    started = time.monotonic()
    expect_reason(fw, f"lpm.enter sleep wake={lp_cfg['wake']} marker={lp_cfg['marker']} timeout={timeout_ms}", reason)
    assert time.monotonic() - started >= timeout_ms / 1000 * (percent / 100)


@then("the board answers ping")
def answers_ping(fw):
    fw.system.ping()


@then(parsers.parse("a firmware delay of {ms:d} ms lasts at least {least_ms:d} ms"))
def tick_resumed(fw, ms, least_ms):
    started = time.monotonic()
    fw.system.delay(ms)
    assert time.monotonic() - started >= least_ms / 1000, "the SysTick tick did not resume"


@then("the wake pin and the marker pin can each be configured as an input and released")
def pins_released(fw, lp_cfg):
    for pin in (lp_cfg["wake"], lp_cfg["marker"]):
        fw.gpio.cfg(pin, "in")
        fw.gpio.release(pin)


@then("every malformed or out-of-range lpm.enter command line fails with its reason")
def errors(fw, lp_cfg):
    wake = lp_cfg["wake"]
    lines = [
        ("lpm.enter", "usage"),
        ("lpm.enter nap", "usage"),
        ("lpm.enter sleep edge=both", "usage"),
        ("lpm.enter sleep timeout=0", "range"),
        ("lpm.enter sleep timeout=10001", "range"),
        (f"lpm.enter sleep wake={wake} marker={wake}", "usage"),
        ("lpm.enter sleep wake=PA16", "pin"),
        (f"lpm.enter sleep wake={lp_cfg['unbonded_pin']}", "pin"),
        (f"lpm.enter sleep marker={lp_cfg['unbonded_pin']}", "pin"),
        ("lpm.enter sleep extra", "usage"),
    ]
    for line, reason in lines:
        expect_reason(fw, line, reason)


@then(
    parsers.parse(
        "with the marker pin configured as a GPIO output, entering sleep with the wake pin and the marker pin for {ms:d} ms fails with "
        '"{reason}", and the marker pin is released even if that fails'
    )
)
def marker_busy(fw, lp_cfg, ms, reason):
    wake, marker = lp_cfg["wake"], lp_cfg["marker"]
    fw.gpio.cfg(marker, "out")
    try:
        expect_reason(fw, f"lpm.enter sleep wake={wake} marker={marker} timeout={ms}", reason)
    finally:
        fw.gpio.release(marker)
