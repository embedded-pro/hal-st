"""Low-power mode through the `lpm` group: `hal::LowPowerModeStm::Enter` inside a window where only the wake line's
EXTI interrupt and the scaffold timer (TIM17, 1 us ticks, also the safety timeout) are enabled and the SysTick tick is
off. The marker pin is low while the core sleeps.

`deep` maps to Sleep on STM32WB/WBA (`LowPowerModeStm::Stop`), so it behaves like `sleep` and never calls the
clock-restore callback (`restored=0`). The wake edge comes from the AD3 on the wake pin (bundle1 `gpio0`); the logic
analyzer sees the marker low before the edge and high right after it.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.firmware import settle


@pytest.fixture
def lp_cfg(board_cfg):
    return board_cfg.param("lowpower")


def expect_reason(fw, line: str, reason: str) -> None:
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


@pytest.mark.ad3
@pytest.mark.board_params("mode", "lowpower.modes")
@pytest.mark.board_params("edge", "lowpower.edges")
def test_wake_on_edge(request, fw, ad3, need, lp_cfg, mode, edge):
    """The core sleeps (marker low) until the edge on the wake pin; the marker rises within `latency_us` of it and
    `us` covers the time asleep."""
    if request.config.getoption("--fake"):
        pytest.skip("the fake AD3 sees no board")
    wake_dio, marker_dio = need.dio(lp_cfg["wake"]), need.dio(lp_cfg["marker"])
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
    assert (wake.woke, wake.restored) == ("exti", 0), wake.raw
    assert delay * 1e6 * 0.5 <= wake.us <= timeout_ms * 1000, wake.raw
    marker = result.channel(marker_dio)
    trigger = result.trigger_index
    assert trigger is not None
    assert all(bit == 0 for bit in marker[:trigger]), "the marker was high before the wake edge"
    rises = [change.index for change in analysis.edges(marker) if change.index >= trigger and change.rising]
    assert rises, "the marker did not rise after the wake edge"
    latency = (rises[0] - trigger) / result.rate
    assert latency <= lp_cfg["latency_us"] * 1e-6, f"woke {latency * 1e6:.1f} us after the edge"


def test_timeout(request, fw, need, lp_cfg):
    """Without an edge the scaffold timer ends the window: `ERR timeout` after `timeout` ms; the board then answers,
    and its tick runs again (`delay` lasts its time)."""
    if request.config.getoption("--fake"):
        pytest.skip("the fake firmware has no wake pin model: it wakes at once")
    need.unloaded(lp_cfg["wake"], lp_cfg["marker"])
    timeout_ms = lp_cfg["timeout_ms"]
    started = time.monotonic()
    expect_reason(fw, f"lpm.enter sleep wake={lp_cfg['wake']} marker={lp_cfg['marker']} timeout={timeout_ms}", "timeout")
    assert time.monotonic() - started >= timeout_ms / 1000 * 0.9
    fw.system.ping()
    started = time.monotonic()
    fw.system.delay(100)
    assert time.monotonic() - started >= 0.09, "the SysTick tick did not resume"


def test_pins_released(fw, lp_cfg):
    """`lpm.enter` frees the wake and marker pins and the scaffold timer, whatever its outcome."""
    try:
        fw.lpm.enter("sleep", wake=lp_cfg["wake"], marker=lp_cfg["marker"], timeout=lp_cfg["timeout_ms"])
    except FirmwareError as error:
        assert error.reason == "timeout"
    for pin in (lp_cfg["wake"], lp_cfg["marker"]):
        fw.gpio.cfg(pin, "in")
        fw.gpio.release(pin)


def test_errors(fw, lp_cfg):
    wake, marker = lp_cfg["wake"], lp_cfg["marker"]
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

    fw.gpio.cfg(marker, "out")
    try:
        expect_reason(fw, f"lpm.enter sleep wake={wake} marker={marker} timeout=1", "busy")
    finally:
        fw.gpio.release(marker)
