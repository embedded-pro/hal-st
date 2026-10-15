"""The `groups.hrtim` model (the driver's prescaler, period, compare and dead-time arithmetic and EMIL's duty
parsing), the fake `hrtim` group (argument order and reasons of validation/firmware/HrtimGroup.cpp, one unit at a
time, its pins and the ADC resource) and the wrapper."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation.fake_firmware import G474_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups.hrtim import (
    adc_trigger_available,
    compare_ticks,
    dead_time,
    duty_cycle,
    format_percent,
    frequency_range,
    high_resolution_clock,
    minimum_ticks,
    timing_for,
)

CLOCK = 170_000_000


def make(family="stm32g474"):
    fake = FakeFirmware(family=family)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, G474_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("frequency", "expected"),
    [
        (100_000, (0, 54_400)),
        (83_100, (0, 65_463)),
        (83_000, (1, 32_771)),
        (10_000, (4, 34_000)),
        (649, (7, 65_485)),
        (648, None),
        (28_333_333, (0, 192)),
        (28_333_334, None),
        (0, None),
    ],
)
def test_timing_picks_the_finest_prescaler_that_fits(frequency, expected):
    assert timing_for(CLOCK, frequency) == expected


def test_clocks_and_limits():
    assert high_resolution_clock(CLOCK, 0) == 5_440_000_000
    assert high_resolution_clock(CLOCK, 7) == 42_500_000
    assert [minimum_ticks(ratio) for ratio in range(8)] == [96, 48, 24, 12, 6, 3, 3, 3]
    assert frequency_range(CLOCK) == (649, 28_333_333)


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("0", 0),
        ("50", 32_768),
        ("100", 65_536),
        ("12.3456", 8_091),
        ("0.0001", 0),
        ("99.99", 65_529),
        ("100.0001", None),
        ("101", None),
        ("1.23456", None),
        ("0x10", None),
        ("", None),
        (".5", None),
        ("5.", None),
    ],
)
def test_duty_cycle_parses_as_emil(text, expected):
    assert duty_cycle(text) == expected


def test_percents_are_written_with_four_decimals_at_most():
    assert format_percent(33.33333) == "33.3333"
    assert format_percent(50) == "50"
    assert duty_cycle(12.5) == duty_cycle("12.5")


def test_compares_stay_clear_of_the_period_ends():
    assert compare_ticks(54_400, 0, duty_cycle("50")) == 27_200
    assert compare_ticks(54_400, 0, 0) == 96
    assert compare_ticks(54_400, 0, duty_cycle("100")) == 54_400 - 96
    assert compare_ticks(192, 0, duty_cycle("50")) == 96


@pytest.mark.parametrize(
    ("ns", "expected"),
    [
        (0, (0, 0)),
        (50, (0, 68)),
        (250, (0, 340)),
        (375, (0, 510)),
        (376, (0, 511)),
        (377, (1, 256)),
        (1000, (2, 340)),
        (10_000, (5, 425)),
        (60_000, (7, 511)),
    ],
)
def test_dead_time_picks_the_finest_prescaler(ns, expected):
    prescaler, rising, falling = dead_time(CLOCK, ns, ns)
    assert (prescaler, rising) == expected and falling == rising


def test_unit_e_has_no_even_adc_triggers():
    assert adc_trigger_available(1, 2) and adc_trigger_available(6, 10)
    assert not adc_trigger_available(5, 2) and adc_trigger_available(5, 3)
    assert not adc_trigger_available(1, 0) and not adc_trigger_available(1, 11)


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("hrtim.open 1", "ok"),
        ("hrtim.open 7 mode=x", "range"),
        ("hrtim.open 1 mode=x freq=0", "usage"),
        ("hrtim.open 1 freq=0 dead=50", "range"),
        ("hrtim.open 1 dead=50 flt=PZ1", "pin"),
        ("hrtim.open 1 dead=50 freq=648", "usage"),
        ("hrtim.open 1 flt=PA8 freq=648", "pin"),
        ("hrtim.open 1 freq=648 adc=11", "range"),
        ("hrtim.open 5 freq=648 adc=2", "range"),
        ("hrtim.open 5 adc=2", "unsupported"),
        ("hrtim.open 6 adc=2", "ok"),
        ("hrtim.open 1 out=2 flt=PB0", "ok"),
        ("hrtim.open 1 flt=PC7", "ok"),
        ("hrtim.open 1 mode=comp deadfall=100", "ok"),
    ],
)
def test_open_argument_order(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_replies_follow_the_model():
    _, _, fw = make()
    timing = fw.hrtim.open(1, mode="comp", freq=200_000, dead=100)
    assert (timing.clock, timing.ratio, timing.period, timing.dead, timing.dtprsc) == (CLOCK, 0, 27_200, 136, 0)
    assert timing.frequency == pytest.approx(200_000)
    assert fw.hrtim.duty(1, 40) == [10_880]
    assert fw.hrtim.duty(1, 100) == [27_200 - 96]
    timing = fw.hrtim.freq(1, 10_000)
    assert (timing.ratio, timing.period, timing.dead) == (4, 34_000, 136)
    assert fw.hrtim.duty(1, "12.5") == [4_250]


def test_two_duties_need_both_outputs_in_phase():
    terminal, _, fw = make()
    fw.hrtim.open(1, freq=100_000)
    assert fw.hrtim.duty(1, 25) == [13_600, 13_600]
    assert fw.hrtim.duty(1, 25, 75) == [13_600, 40_800]
    fw.hrtim.close(1)
    fw.hrtim.open(1, out="1")
    assert reason(terminal, "hrtim.duty 1 25 75") == "usage"
    assert fw.hrtim.duty(1, 25) == [8_500]


def test_phase_needs_the_shifted_outputs():
    terminal, _, fw = make()
    fw.hrtim.open(1)
    assert reason(terminal, "hrtim.phase 1 10") == "usage"
    fw.hrtim.close(1)
    fw.hrtim.open(1, mode="shift")
    assert reason(terminal, "hrtim.phase 1 x") == "usage"
    assert reason(terminal, "hrtim.phase 1 100") == "range"
    fw.hrtim.phase(1, "99.99")


def test_one_unit_and_its_pins():
    terminal, fake, fw = make()
    fw.hrtim.open(1, flt="PA12")
    assert {pin for pin, (owner, _) in fake.claims.items() if owner == ("hrtim", "1")} == {"PA8", "PA9", "PA12"}
    assert reason(terminal, "hrtim.open 2") == "busy"
    assert reason(terminal, "gpio.cfg PA12 in") == "busy"
    assert not fw.hrtim.fault(1)
    fw.hrtim.close(1)
    assert reason(terminal, "gpio.cfg PA12 in") == "ok"
    fw.hrtim.open(2, out="1")
    assert {pin for pin, (owner, _) in fake.claims.items() if owner == ("hrtim", "2")} == {"PA10"}


def test_adc_counts_at_the_pwm_frequency_while_running():
    terminal, fake, fw = make()
    fw.hrtim.open(1, out="1", freq=10_000, adc=1)
    assert reason(terminal, "hrtim.adc 1 10") == "timeout"
    fw.hrtim.duty(1, 50)
    assert fw.hrtim.adc(1, 201) == 20_000
    fake.resources[("adc", 1)] = ("adc", "0")
    assert reason(terminal, "hrtim.adc 1 10") == "busy"
    fw.hrtim.stop(1)
    fw.hrtim.close(1)
    fw.hrtim.open(1, adc=4)
    assert reason(terminal, "hrtim.adc 1 10") == "unsupported"


@pytest.mark.parametrize("family", ["stm32wb55", "stm32wba55"])
def test_unsupported_off_the_g474(family):
    terminal, _, _ = make(family)
    for line in ("hrtim.open 1", "hrtim.duty 1 50", "hrtim.adc 1 2", "hrtim.close 1"):
        assert reason(terminal, line) == "unsupported"
