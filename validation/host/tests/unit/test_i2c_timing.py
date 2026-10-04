"""`i2c.expected_timing`, the host twin of validation/firmware/I2cTiming.cpp: the TIMINGR table of PROTOCOL.md (D.1)
and reference vectors of the design's `i2c_timing.py` `compute()`, copied as literals, plus the I2C limits every
result must meet with a zero rise time."""

import pytest

from hal_st_validation.i2c import (
    BUS_MAX,
    BUS_MIN,
    FAST_MODE,
    STANDARD_MODE,
    arbitration_window,
    bit_times,
    expected_timing,
    mode_limits,
    timing_fields,
)

PROTOCOL_TABLE = [
    (16_000_000, 20_000, 0x20408186),
    (16_000_000, 50_000, 0x00E097A2),
    (16_000_000, 100_000, 0x00E04752),
    (16_000_000, 400_000, 0x00500916),
    (32_000_000, 20_000, 0x80305659),
    (32_000_000, 50_000, 0x2090656C),
    (32_000_000, 100_000, 0x10E04853),
    (32_000_000, 400_000, 0x00B0162E),
    (64_000_000, 20_000, 0x90509CA1),
    (64_000_000, 50_000, 0x80604348),
    (64_000_000, 100_000, 0x40B03943),
    (64_000_000, 400_000, 0x10B0172F),
    (100_000_000, 20_000, 0xD060AEB4),
    (100_000_000, 50_000, 0x70B0777F),
    (100_000_000, 100_000, 0x50E04B57),
    (100_000_000, 400_000, 0x20B11931),
]

REFERENCE_GRID = [
    (16_000_000, 75_000, 0x00E0616D),
    (16_000_000, 100_001, 0x00504552),
    (16_000_000, 250_000, 0x00501522),
    (24_000_000, 20_000, 0x2070C3C9),
    (24_000_000, 75_000, 0x10A04952),
    (24_000_000, 100_001, 0x00806A7C),
    (24_000_000, 250_000, 0x00802234),
    (24_000_000, 400_000, 0x00801022),
    (48_000_000, 20_000, 0x70509297),
    (48_000_000, 75_000, 0x30A04953),
    (48_000_000, 100_001, 0x10806B7D),
    (48_000_000, 250_000, 0x10802335),
    (48_000_000, 400_000, 0x10801123),
    (80_000_000, 20_000, 0x8070D9E0),
    (80_000_000, 75_000, 0x40E0626F),
    (80_000_000, 100_001, 0x10D0B3D1),
    (80_000_000, 250_000, 0x10D03B59),
    (80_000_000, 400_000, 0x10D01D3B),
]


@pytest.mark.parametrize(("kernel", "bus", "timing"), PROTOCOL_TABLE)
def test_protocol_table(kernel, bus, timing):
    assert expected_timing(kernel, bus) == timing


@pytest.mark.parametrize(("kernel", "bus", "timing"), REFERENCE_GRID)
def test_reference_grid(kernel, bus, timing):
    assert expected_timing(kernel, bus) == timing


@pytest.mark.parametrize("bus", [0, BUS_MIN - 1, BUS_MAX + 1, 1_000_000])
def test_bus_outside_the_range(bus):
    assert expected_timing(64_000_000, bus) is None


def test_no_kernel_clock():
    assert expected_timing(0, 100_000) is None


@pytest.mark.parametrize("kernel", [16_000_000, 32_000_000, 64_000_000, 100_000_000])
@pytest.mark.parametrize("bus", [BUS_MIN, 33_333, 50_000, 99_999, 100_000, 100_001, 200_000, 333_333, BUS_MAX])
def test_limits_with_zero_rise(kernel, bus):
    """The `check(..., trise=0)` of the design: tLOW/tHIGH minima of the mode, SCLDEL covers rise + setup, SDADEL
    stays below the valid-data limit, and the period with zero rise is never shorter than 1/bus."""
    timing = expected_timing(kernel, bus)
    assert timing is not None
    mode = mode_limits(bus)
    fields = timing_fields(timing)
    clock = 1e12 / kernel
    prescaled = (fields["presc"] + 1) * clock
    low, high = bit_times(timing, kernel)
    assert low * 1e12 > mode.lscl_min
    assert high * 1e12 >= mode.hscl_min - 1e-3
    assert (fields["scldel"] + 1) * prescaled >= mode.rise + mode.sudat_min
    assert (fields["sdadel"] * (fields["presc"] + 1) + 1) * clock <= mode.vddat_max - mode.rise - 260_000 - 4 * clock
    assert 1 / (low + high + mode.fall / 1e12) <= bus * 1.0001


def test_mode_split():
    assert mode_limits(100_000) is STANDARD_MODE
    assert mode_limits(100_001) is FAST_MODE
    assert STANDARD_MODE.tlow_min == pytest.approx(4.7e-6)
    assert FAST_MODE.thigh_min == pytest.approx(0.6e-6)


def test_timing_fields():
    assert timing_fields(0x10B0172F) == {"presc": 1, "scldel": 0xB, "sdadel": 0, "sclh": 0x17, "scll": 0x2F}
    assert timing_fields(0x70B03D3D) == {"presc": 7, "scldel": 0xB, "sdadel": 0, "sclh": 0x3D, "scll": 0x3D}


def test_bit_times_of_the_default_timing():
    """0x70B03D3D at 64 MHz: 62 prescaled clocks of 125 ns each plus 81.25 ns of filter and sync per phase."""
    low, high = bit_times(0x70B03D3D, 64_000_000)
    assert low == pytest.approx(62 * 125e-9 + 50e-9 + 2 / 64e6)
    assert high == pytest.approx(low)


def test_arbitration_window_covers_the_second_address_bit():
    low, high = bit_times(0x40B03943, 64_000_000)
    delay, width = arbitration_window(0x40B03943, 64_000_000)
    second_high = (2 * high + 2 * low, 3 * high + 2 * low)
    assert delay < second_high[0] and delay + width > second_high[1]
    assert width == pytest.approx(low + high)
