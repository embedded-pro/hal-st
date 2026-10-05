"""Low-power timers (`hal::FreeRunningLowPowerTimerStm`, `hal::LowPowerTimerWithInterruptStm`) through the `lptim`
group: update rate on the marker pin for every prescaler, the WBA repetition counter, interrupt counts, the
free-running counter, stop, and the LPTIM shared with the LPTIM encoder (`qei lp=1`) and `lptpwm`.

Wiring set `bundle1`: `tests.lptim.marker` (gpio0) on a DIO; the marker toggles once per update interrupt, so it runs
at half the update rate `lptimclk / prescaler / (period + 1) / (rep + 1)`, with the LPTIM kernel clock `lptimclk` of
the board file's `clocks.lptim` (`lptim.open` reports it, test_reports_the_kernel_clock).
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

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
def test_reports_the_kernel_clock(fw, board_cfg, instance):
    index = instance["index"]
    assert fw.lptim.open(index, irq="none") == board_cfg.clock("lptim", index)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.matrix("lptim.interrupt")
@pytest.mark.constraint(valid=dispatchable)
def test_update_marker(fw, ad3, need, board_cfg, lptim_cfg, instance, irq, update):
    """The marker toggles on every update interrupt: it runs at half the update rate."""
    dio = need.dio(lptim_cfg["marker"])
    index = instance["index"]
    fw.lptim.open(index, prescaler=update["prescaler"], period=update["period"], irq=irq, pin=lptim_cfg["marker"])
    fw.lptim.start(index)
    lptimclk = board_cfg.clock("lptim", index)
    marker = lptim_update_rate(lptimclk, update["prescaler"], update["period"]) / 2
    assert marker_frequency(ad3, dio, lptim_cfg, marker) == pytest.approx(marker, rel=lptim_cfg["tolerance"]["frequency"])


@pytest.mark.ad3
@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.board_params("prescaler", "lptim.prescalers")
def test_prescaler(fw, ad3, need, board_cfg, lptim_cfg, instance, prescaler):
    """Every divider of the LPTIM clock (1 to 128) divides the update rate."""
    dio = need.dio(lptim_cfg["marker"])
    index, period = instance["index"], lptim_cfg["prescaler_period"]
    fw.lptim.open(index, prescaler=prescaler, period=period, irq="immediate", pin=lptim_cfg["marker"])
    fw.lptim.start(index)
    lptimclk = board_cfg.clock("lptim", index)
    marker = lptim_update_rate(lptimclk, prescaler, period) / 2
    assert marker_frequency(ad3, dio, lptim_cfg, marker) == pytest.approx(marker, rel=lptim_cfg["tolerance"]["frequency"])


@pytest.mark.ad3
@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.board_params("rep", "lptim.repetitions")
def test_repetition_counter(fw, ad3, need, board_cfg, lptim_cfg, instance, rep):
    """`rep` (WBA LPTIM): one update every `rep + 1` periods."""
    dio = need.dio(lptim_cfg["marker"])
    index, timing = instance["index"], lptim_cfg["repetition_timing"]
    marker_pin = lptim_cfg["marker"]
    fw.lptim.open(index, prescaler=timing["prescaler"], period=timing["period"], rep=rep, irq="immediate", pin=marker_pin)
    fw.lptim.start(index)
    lptimclk = board_cfg.clock("lptim", index)
    marker = lptim_update_rate(lptimclk, timing["prescaler"], timing["period"], rep) / 2
    assert marker_frequency(ad3, dio, lptim_cfg, marker) == pytest.approx(marker, rel=lptim_cfg["tolerance"]["frequency"])


@pytest.mark.board_params("instance", "lptim.instances")
@pytest.mark.matrix("lptim.interrupt")
@pytest.mark.constraint(valid=dispatchable)
def test_interrupt_count(fw, lptim_cfg, instance, irq, update):
    """`irqs` follows the update rate; dispatched callbacks at or below 1 kHz are not lost."""
    index = instance["index"]
    lptimclk = fw.lptim.open(index, prescaler=update["prescaler"], period=update["period"], irq=irq)
    rate = lptim_update_rate(lptimclk, update["prescaler"], update["period"])
    fw.lptim.start(index)
    start = time.monotonic()
    before = fw.lptim.count(index)
    time.sleep(lptim_cfg["window_s"])
    after = fw.lptim.count(index)
    elapsed = time.monotonic() - start
    tolerance = lptim_cfg["tolerance"]
    counted = after.irqs - before.irqs
    assert counted >= rate * elapsed * (1 - tolerance["count"]) - rate * tolerance["latency_s"], (counted, rate * elapsed)
    assert counted <= rate * elapsed * (1 + tolerance["count"]) + 1, (counted, rate * elapsed)


@pytest.mark.board_params("instance", "lptim.instances")
def test_free_running_counter_and_stop(fw, lptim_cfg, instance):
    """`irq=none` counts without interrupts; `lptim.stop` freezes the counter; repeated start and stop are harmless."""
    index, free = instance["index"], lptim_cfg["free_running"]
    fw.lptim.open(index, prescaler=free["prescaler"], period=free["period"], irq="none")
    assert fw.lptim.count(index).irqs == 0
    fw.lptim.start(index)
    fw.lptim.start(index)
    readings = set()
    for _ in range(5):
        reading = fw.lptim.count(index)
        assert reading.irqs == 0 and reading.cnt <= free["period"]
        readings.add(reading.cnt)
    assert len(readings) > 1, "the counter does not count"
    fw.lptim.stop(index)
    fw.lptim.stop(index)
    stopped = fw.lptim.count(index)
    time.sleep(lptim_cfg["window_s"])
    assert fw.lptim.count(index) == stopped


@pytest.mark.ad3
@pytest.mark.board_params("instance", "lptim.instances")
def test_stop_stops_the_marker(fw, ad3, need, lptim_cfg, instance):
    dio = need.dio(lptim_cfg["marker"])
    index, update = instance["index"], lptim_cfg["interrupt"]["update"][1]
    lptimclk = fw.lptim.open(index, prescaler=update["prescaler"], period=update["period"], irq="immediate", pin=lptim_cfg["marker"])
    fw.lptim.start(index)
    fw.lptim.stop(index)
    marker = lptim_update_rate(lptimclk, update["prescaler"], update["period"]) / 2
    capture = ad3.logic.record_for(lptim_cfg["record_periods"] / marker)
    assert len(set(capture.channel(dio))) == 1, "the marker moved after lptim.stop"


def test_repetition_support(fw, lptim_cfg):
    """`rep` 0..255 on the WBA LPTIM; the STM32WB LPTIM has no repetition counter (`ERR unsupported`)."""
    index = lptim_cfg["instances"][0]["index"]
    expect_error("range", fw.lptim.open, index, rep=LPTIM_REPETITION_MAX + 1)
    if not lptim_cfg["repetition"]:
        expect_error("unsupported", fw.lptim.open, index, rep=0)
        return
    fw.lptim.open(index, rep=LPTIM_REPETITION_MAX)
    fw.lptim.close(index)


def test_open_errors(fw, board_cfg, lptim_cfg):
    """Argument errors in the protocol order: usage, range, pin."""
    index, marker = lptim_cfg["instances"][0]["index"], lptim_cfg["marker"]
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
        expect_error(reason, fw.lptim.open, index, **options)
    assert fw.lptim.open(index, period=1, irq="none") > 0, "the update interrupt rate limit needs an interrupt"
    fw.lptim.close(index)
    for missing in lptim_cfg["missing"]:
        expect_error("range", fw.lptim.open, missing)
    for command in ("start", "stop", "count", "close"):
        expect_error("notopen", getattr(fw.lptim, command), index)
    assert fw.terminal.command(f"lptim.open {index} mode=down", check=False).reason == "usage"
    assert fw.terminal.command(f"lptim.open {index} pin=nosuchalias", check=False).reason == "pin"


def test_one_lptim_at_a_time(fw, lptim_cfg):
    first, second = (instance["index"] for instance in lptim_cfg["instances"][:2])
    fw.lptim.open(first, irq="none")
    expect_error("busy", fw.lptim.open, second, irq="none")


def test_marker_is_released_on_close(fw, need, lptim_cfg):
    index, marker = lptim_cfg["instances"][0]["index"], lptim_cfg["marker"]
    need.unloaded(marker)
    fw.lptim.open(index, irq="immediate", pin=marker)
    expect_error("busy", fw.gpio.cfg, marker, "in")
    fw.lptim.close(index)
    fw.gpio.cfg(marker, "in")


@pytest.mark.board_params("encoder", "qei.lp_instances")
def test_lptim_shared_with_the_encoder(fw, board_cfg, encoder):
    """An LPTIM serves one group (`ResourceAllocation` lpTimer): the LPTIM encoder, `lptim` and `lptpwm`."""
    index = encoder["index"]
    fw.qei.open(index, lp=True, a=encoder["a"], b=encoder["b"])
    expect_error("busy", fw.lptim.open, index, irq="none")
    pwm = next((entry for entry in board_cfg.param("lptim_pwm.instances", []) if entry["index"] == index), None)
    if pwm is not None:
        expect_error("busy", fw.lptpwm.open, index, pins=pwm["pins"])
    fw.qei.close(index)
    fw.lptim.open(index, irq="none")
    expect_error("busy", fw.qei.open, index, lp=True, a=encoder["a"], b=encoder["b"])
    fw.lptim.close(index)
    if pwm is not None:
        fw.lptpwm.open(index, pins=pwm["pins"])
        expect_error("busy", fw.lptim.open, index, irq="none")
