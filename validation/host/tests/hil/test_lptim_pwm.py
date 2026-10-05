"""LPTIM PWM (`hal::LpTimerPwmWithChannels<N>`, `hal::LpPwmChannelGpio`, STM32WBA55 only: LpTimerPwmStm is not
built for STM32WB) through the `lptpwm` group: frequency and duty per channel, `SetPulse`, an unused channel and the
argument checks.

Wiring set `bundle1`: LPTIM1 CH2 (PA15) and LPTIM2 CH1/CH2 (PA11/PA1) on DIOs. The tests assert the duty that
`lptpwm.duty` asks for (CCR = ARR * duty / 100, high while the counter is below CCR). If the bench measures 1 - duty
instead (the output polarity of the LPTIM IP, DESIGN R14), the measured value is in the assertion message: record
it as a known gap rather than changing the expectation. The LPTIM takes compare writes only while it is enabled, so
the tests set duties after `lptpwm.start`.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.groups.timers import LPTIM_PERIOD_MAX, lptim_update_rate, pwm_duty_fraction

pytestmark = pytest.mark.family("stm32wba55")


@pytest.fixture
def lptpwm_cfg(board_cfg):
    return board_cfg.param("lptim_pwm")


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def record(ad3, cfg, frequency):
    periods = cfg["record_periods"]
    return ad3.logic.record_for(periods / frequency, timeout=periods / frequency + 2)


def check_channel(capture, dio, frequency, fraction, cfg):
    assert capture.frequency(dio) == pytest.approx(frequency, rel=cfg["tolerance"]["frequency"]), f"DIO{dio} frequency"
    measured = capture.duty(dio)
    quantisation = 2 * frequency / capture.rate
    assert measured == pytest.approx(fraction, abs=cfg["tolerance"]["duty"] + quantisation), (
        f"DIO{dio}: high fraction {measured:.4f}, expected {fraction:.4f} (1 - duty would be {1 - fraction:.4f})"
    )


@pytest.mark.ad3
@pytest.mark.board_params("instance", "lptim_pwm.instances")
@pytest.mark.board_params("duty", "lptim_pwm.duties")
def test_duty(fw, ad3, need, board_cfg, lptpwm_cfg, instance, duty):
    """Frequency lptimclk / prescaler / (period + 1) and the duty of every wired channel."""
    dios = [None if pin is None else need.dio(pin) for pin in instance["pins"]]
    index = instance["index"]
    lptimclk = fw.lptpwm.open(index, pins=instance["pins"], prescaler=lptpwm_cfg["prescaler"], period=lptpwm_cfg["period"])
    assert lptimclk == board_cfg.clock("lptim", index)
    fw.lptpwm.start(index)
    for channel, dio in enumerate(dios, 1):
        if dio is not None:
            fw.lptpwm.duty(index, channel, duty)
    frequency = lptim_update_rate(lptimclk, lptpwm_cfg["prescaler"], lptpwm_cfg["period"])
    capture = record(ad3, lptpwm_cfg, frequency)
    for dio in dios:
        if dio is not None:
            check_channel(capture, dio, frequency, pwm_duty_fraction(lptpwm_cfg["period"], duty), lptpwm_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "lptim_pwm.instances")
def test_pulse(fw, ad3, need, lptpwm_cfg, instance):
    """`SetPulse(on, period)`: the channel's compare value and the shared auto-reload."""
    channel = next(position for position, pin in enumerate(instance["pins"], 1) if pin is not None)
    dio = need.dio(instance["pins"][channel - 1])
    index, pulse = instance["index"], lptpwm_cfg["pulse"]
    lptimclk = fw.lptpwm.open(index, pins=instance["pins"], prescaler=lptpwm_cfg["prescaler"], period=lptpwm_cfg["period"])
    fw.lptpwm.start(index)
    fw.lptpwm.pulse(index, channel, pulse["ccr"], pulse["period"])
    frequency = lptim_update_rate(lptimclk, lptpwm_cfg["prescaler"], pulse["period"])
    check_channel(record(ad3, lptpwm_cfg, frequency), dio, frequency, pulse["ccr"] / (pulse["period"] + 1), lptpwm_cfg)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "lptim_pwm.instances")
def test_stop(fw, ad3, need, lptpwm_cfg, instance):
    """`lptpwm.stop` stops the outputs; start again resumes them; repeating either is harmless."""
    dios = [need.dio(pin) for pin in instance["pins"] if pin is not None]
    index = instance["index"]
    lptimclk = fw.lptpwm.open(index, pins=instance["pins"], prescaler=lptpwm_cfg["prescaler"], period=lptpwm_cfg["period"])
    fw.lptpwm.start(index)
    fw.lptpwm.start(index)
    for channel, pin in enumerate(instance["pins"], 1):
        if pin is not None:
            fw.lptpwm.duty(index, channel, 50)
    fw.lptpwm.stop(index)
    fw.lptpwm.stop(index)
    frequency = lptim_update_rate(lptimclk, lptpwm_cfg["prescaler"], lptpwm_cfg["period"])
    capture = record(ad3, lptpwm_cfg, frequency)
    stopped = lptpwm_cfg["record_periods"] // 4
    assert all(analysis.edge_count(capture.channel(dio), "rising") <= stopped for dio in dios), "an output runs after lptpwm.stop"
    fw.lptpwm.start(index)
    for channel, pin in enumerate(instance["pins"], 1):
        if pin is not None:
            fw.lptpwm.duty(index, channel, 50)
    capture = record(ad3, lptpwm_cfg, frequency)
    for dio in dios:
        check_channel(capture, dio, frequency, pwm_duty_fraction(lptpwm_cfg["period"], 50), lptpwm_cfg)


def test_open_errors(fw, lptpwm_cfg, board_cfg):
    """Argument errors in the protocol order: usage, range, pin."""
    instance = next(entry for entry in lptpwm_cfg["instances"] if entry["pins"][0] is not None)
    index, pins = instance["index"], instance["pins"]
    cases = [
        ({"pins": [None, None]}, "usage"),
        ({"pins": [*pins, pins[0]]}, "usage"),
        ({"pins": pins, "prescaler": 3}, "range"),
        ({"pins": pins, "prescaler": 256}, "range"),
        ({"pins": pins, "period": 0}, "range"),
        ({"pins": pins, "period": LPTIM_PERIOD_MAX + 1}, "range"),
        ({"pins": [lptpwm_cfg["foreign_pin"]]}, "pin"),
        ({"pins": list(reversed(pins))}, "pin"),
    ]
    for options, reason in cases:
        expect_error(reason, fw.lptpwm.open, index, **options)
    for line, reason in ((f"lptpwm.open {index}", "usage"), (f"lptpwm.open {index} pins=nosuchalias", "pin")):
        assert fw.terminal.command(line, check=False).reason == reason, line
    for missing in board_cfg.param("lptim.missing"):
        expect_error("range", fw.lptpwm.open, missing, pins=pins)
    for command, args in (("duty", (1, 50)), ("pulse", (1, 1, 2)), ("start", ()), ("stop", ()), ("close", ())):
        expect_error("notopen", getattr(fw.lptpwm, command), index, *args)
    fw.lptpwm.open(index, pins=pins)
    for command, args in (("duty", (3, 50)), ("duty", (1, 101)), ("pulse", (1, LPTIM_PERIOD_MAX + 1, 2)), ("pulse", (1, 1, 0))):
        expect_error("range", getattr(fw.lptpwm, command), index, *args)


def test_one_lptim_pwm_at_a_time_and_pins_claimed(fw, need, lptpwm_cfg):
    first, second = lptpwm_cfg["instances"][:2]
    need.unloaded(*(pin for pin in first["pins"] if pin is not None))
    pin = next(pin for pin in first["pins"] if pin is not None)
    fw.lptpwm.open(first["index"], pins=first["pins"])
    expect_error("busy", fw.lptpwm.open, second["index"], pins=second["pins"])
    expect_error("busy", fw.lptim.open, first["index"], irq="none")
    expect_error("busy", fw.gpio.cfg, pin, "in")
    fw.lptpwm.close(first["index"])
    fw.gpio.cfg(pin, "in")
