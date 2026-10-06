import pytest

from hal_st_validation import expect

WB55 = 64_000_000
WBA55 = 100_000_000


def test_timer_features():
    assert expect.timer_counter_max(2) == 0xFFFFFFFF
    assert expect.timer_counter_max(1) == expect.timer_counter_max(16) == 0xFFFF
    assert [timer for timer in (1, 2, 3, 16, 17) if expect.timer_has_break(timer)] == [1, 16, 17]
    assert [timer for timer in (1, 2, 3, 16, 17) if expect.timer_has_center_mode(timer)] == [1, 2, 3]
    assert sorted(expect.ENCODER_TIMERS) == [1, 2, 3]
    assert sorted(expect.ADC_TRIGGER_TIMERS) == [1, 2]


def test_pwm_clock_and_ticks():
    assert expect.pwm_clock(WB55) == 64_000_000
    assert expect.pwm_clock(WB55, 63) == 1_000_000
    assert expect.pwm_clock(WBA55, 999) == 100_000
    assert expect.pwm_clock(WB55, 65535) == 976
    assert expect.pwm_ticks(WB55, 10000, "edge") == 6400
    assert expect.pwm_ticks(WB55, 10000, "center") == 3200
    assert expect.pwm_ticks(WBA55, 30000, "edge") == 3333
    assert expect.pwm_ticks(WBA55, 30000, "center") == 1666


@pytest.mark.parametrize(
    ("pwmclk", "freq", "mode", "counter_max", "fits"),
    [
        (WB55, 1000, "edge", 0xFFFF, True),
        (WB55, 976, "edge", 0xFFFF, False),
        (WB55, 977, "edge", 0xFFFF, True),
        (WB55, 100, "edge", 0xFFFF, False),
        (WB55, 100, "edge", 0xFFFFFFFF, True),
        (WB55, 489, "center", 0xFFFF, True),
        (WB55, 488, "center", 0xFFFF, False),
        (WB55, 32_000_000, "edge", 0xFFFF, True),
        (WB55, 32_000_001, "edge", 0xFFFF, False),
        (WB55, 16_000_000, "center", 0xFFFF, True),
        (WB55, 16_000_001, "center", 0xFFFF, False),
        (WB55, 21_333_334, "center", 0xFFFF, False),
        (WBA55, 1000, "edge", 0xFFFF, False),
        (WBA55, 1000, "center", 0xFFFF, True),
        (WBA55, 1526, "edge", 0xFFFF, True),
        (WBA55, 1525, "edge", 0xFFFF, False),
        (976, 100, "edge", 0xFFFF, True),
        (976, 1000, "edge", 0xFFFF, False),
        (WB55, 0, "edge", 0xFFFF, False),
    ],
)
def test_pwm_fits(pwmclk, freq, mode, counter_max, fits):
    assert expect.pwm_fits(pwmclk, freq, mode, counter_max) is fits


def test_pwm_frequency_quantisation():
    assert expect.pwm_frequency(WB55, 10000, "edge") == pytest.approx(10000)
    assert expect.pwm_frequency(WBA55, 30000, "edge") == pytest.approx(WBA55 / 3333)
    assert expect.pwm_frequency(WB55, 10000, "center") == pytest.approx(10000), "2 * ARR ticks per period, ARR = 3200"
    assert expect.pwm_frequency(WBA55, 30000, "center") == pytest.approx(WBA55 / 3332)
    assert expect.pwm_frequency(1_000_000, 300_000, "edge") == pytest.approx(333_333.33, rel=1e-6)


def test_pwm_auto_reload():
    """ARR = ticks - 1 edge aligned, ticks / 2 centre aligned (2 * ARR ticks per period)."""
    assert expect.pwm_auto_reload(WB55, 10000, "edge") == 6399
    assert expect.pwm_auto_reload(WB55, 10000, "center") == 3200
    assert expect.pwm_auto_reload(WB55, 32_000_000, "center") == 1
    assert expect.pwm_frequency(WB55, 21_333_334, "center") == pytest.approx(32_000_000), "3 ticks: ARR 1"


@pytest.mark.parametrize(
    ("pwmclk", "mode", "counter_max", "limits"),
    [
        (WB55, "edge", 0xFFFF, (977, 32_000_000)),
        (WB55, "center", 0xFFFF, (489, 16_000_000)),
        (WB55, "edge", 0xFFFFFFFF, (1, 32_000_000)),
        (WBA55, "edge", 0xFFFF, (1526, 50_000_000)),
        (WBA55, "center", 0xFFFF, (763, 25_000_000)),
        (976, "edge", 0xFFFF, (1, 488)),
        (976, "center", 0xFFFF, (1, 244)),
    ],
)
def test_pwm_frequency_limits(pwmclk, mode, counter_max, limits):
    assert expect.pwm_frequency_limits(pwmclk, mode, counter_max) == limits
    low, high = limits
    assert expect.pwm_fits(pwmclk, low, mode, counter_max) and expect.pwm_fits(pwmclk, high, mode, counter_max)
    assert not expect.pwm_fits(pwmclk, low - 1, mode, counter_max)
    assert not expect.pwm_fits(pwmclk, high + 1, mode, counter_max)


def test_pwm_duty():
    assert expect.pwm_duty_counts(WB55, 10000, "edge", 12.5) == 800
    assert expect.pwm_duty(WB55, 10000, "edge", 12.5) == pytest.approx(12.5)
    assert expect.pwm_duty_counts(1_000_000, 100_000, "edge", 12.5) == 1, "10 ticks: 1.25 rounds to 1"
    assert expect.pwm_duty(1_000_000, 100_000, "edge", 12.5) == pytest.approx(10)
    assert expect.pwm_duty(WB55, 10000, "center", 100) == pytest.approx(100)
    assert expect.pwm_duty(1_000_000, 100_000, "center", 50) == pytest.approx(60), "5 ticks: 2.5 rounds to 3"
    assert expect.pwm_duty_step(WB55, 10000, "edge") == pytest.approx(100 / 6400)
    assert expect.pwm_duty_step(WB55, 10000, "center") == pytest.approx(100 / 3200)


@pytest.mark.parametrize(
    ("pwmclk", "freq", "mode", "duty", "resolvable"),
    [
        (1_000_000, 100_000, "edge", 50, True),
        (1_000_000, 100_000, "edge", 4, False),
        (1_000_000, 100_000, "edge", 96, False),
        (1_000_000, 100_000, "edge", 90, True),
        (1_000_000, 100_000, "center", 90, False),
        (1_000_000, 100_000, "center", 80, True),
        (1_000_000, 100_000, "center", 12.5, True),
        (1_000_000, 100_000, "center", 5, False),
        (1_000_000, 50_000, "center", 12.5, True),
        (WB55, 10000, "edge", 12.5, True),
    ],
)
def test_pwm_duty_resolvable(pwmclk, freq, mode, duty, resolvable):
    """10 ticks edge aligned (CCR 1-9 keep both edges), 5 ticks centre aligned (ARR 5: CCR 1-4)."""
    assert expect.pwm_duty_resolvable(pwmclk, freq, mode, duty) is resolvable


def test_pwm_prescaler_for():
    assert expect.pwm_prescaler_for(WB55, 1000, "edge") == 0
    assert expect.pwm_prescaler_for(WB55, 100, "edge") == 9
    assert expect.pwm_prescaler_for(WB55, 100, "edge", 0xFFFFFFFF) == 0
    assert expect.pwm_prescaler_for(WBA55, 1000, "edge") == 1
    assert expect.pwm_prescaler_for(WBA55, 1, "center") == 762
    with pytest.raises(ValueError):
        expect.pwm_prescaler_for(WB55, 40_000_000, "edge")


@pytest.mark.parametrize(
    ("dead_ns", "clock", "ticks"),
    [
        (0, WB55, 0),
        (1, WB55, 1),
        (100, WB55, 7),
        (1984, WB55, 127),
        (2000, WB55, 128),
        (2010, WB55, 130),
        (5000, WB55, 320),
        (10000, WB55, 640),
        (15750, WB55, 1008),
        (15751, WB55, 1008),
        (20000, WB55, 1008),
        (1_000_000, WB55, 1008),
        (100, WBA55, 10),
        (1280, WBA55, 128),
        (2550, WBA55, 256),
        (5050, WBA55, 512),
        (10080, WBA55, 1008),
    ],
)
def test_pwm_dead_ticks(dead_ns, clock, ticks):
    """DTG encodes 0-127 ticks, then 2-, 8- and 16-tick steps up to 1008, rounding up."""
    assert expect.pwm_dead_ticks(dead_ns, clock) == ticks


def test_pwm_dead_time_and_limits():
    assert expect.pwm_dead_time(100, WB55) == pytest.approx(7 / WB55)
    assert expect.pwm_dead_time(20000, WB55) == pytest.approx(15.75e-6)
    assert expect.pwm_dead_time(20000, WBA55) == pytest.approx(10.08e-6)
    assert expect.pwm_dead_fits(1_000_000)
    assert not expect.pwm_dead_fits(1_000_001)
    assert not expect.pwm_dead_fits(-1)


@pytest.mark.parametrize(
    ("spiclk", "baud", "divider"),
    [
        (WB55, 32_000_000, 2),
        (WB55, 31_999_999, 4),
        (WB55, 3_000_000, 32),
        (WB55, 1_000_000, 64),
        (WB55, 250_000, 256),
        (WBA55, 50_000_000, 2),
        (WBA55, 3_000_000, 64),
        (WBA55, 6_250_000, 16),
        (WBA55, 390_625, 256),
    ],
)
def test_spi_divider(spiclk, baud, divider):
    assert expect.spi_divider(spiclk, baud) == divider
    assert expect.spi_clock(spiclk, baud) == spiclk / divider
    assert expect.spi_clock(spiclk, baud) <= baud


def test_spi_range():
    assert expect.spi_baud_fits(WB55, 250_000)
    assert not expect.spi_baud_fits(WB55, 249_999)
    assert expect.spi_baud_fits(WB55, 32_000_000)
    assert not expect.spi_baud_fits(WB55, 32_000_001)
    assert not expect.spi_baud_fits(WBA55, 390_624)
    with pytest.raises(ValueError):
        expect.spi_divider(WBA55, 50_000_001)


def test_uart_dividers():
    assert expect.uart_divider(WB55, 115200, lp=True) == 142222
    assert expect.uart_divider(WB55, 115200) == 1111
    assert expect.uart_divider(WBA55, 921600) == 217
    assert expect.uart_actual_baud(WBA55, 921600) == pytest.approx(921659, rel=1e-6)
    assert expect.uart_clock_name(1, lp=True) == "lpuart1"
    assert expect.uart_clock_name(2) == "usart2"


@pytest.mark.parametrize(
    ("clock", "lp", "maximum", "limits"),
    [
        (WB55, True, 8_000_000, (15626, 8_000_000)),
        (WB55, True, 12_000_000, (15626, 12_000_000)),
        (WBA55, True, 12_000_000, (24415, 12_000_000)),
        (WBA55, False, 12_000_000, (3052, 12_000_000)),
        (WB55, False, 12_000_000, (1954, 8_258_064)),
        (1_000_000, True, 12_000_000, (300, 333_550)),
    ],
)
def test_uart_baud_limits(clock, lp, maximum, limits):
    """LPUART: BRR 0x300-0xFFFFF; USART (8x oversampling): BRR 16-65535; both rounded to nearest like the HAL."""
    assert expect.uart_baud_limits(clock, lp, maximum) == limits
    low, high = limits
    assert expect.uart_baud_fits(clock, low, lp, maximum) and expect.uart_baud_fits(clock, high, lp, maximum)
    assert not expect.uart_baud_fits(clock, low - 1, lp, maximum)
    assert not expect.uart_baud_fits(clock, high + 1, lp, maximum)


def test_uart_baud_max_follows_the_hal():
    """The WB HAL asserts baud < 8000001 (stm32wbxx_hal_uart.h IS_UART_BAUDRATE), the WBA HAL baud < 12500000."""
    assert expect.uart_baud_max("stm32wb55") == 8_000_000
    assert expect.uart_baud_max("stm32wba55") == 12_000_000
    assert expect.uart_baud_max("other") == 12_000_000
    assert not expect.uart_baud_fits(WB55, 8_000_001, lp=True, maximum=expect.uart_baud_max("stm32wb55"))


def test_uart_frames():
    assert expect.uart_frame_bits("none") == 10
    assert expect.uart_frame_bits("even") == 11
    assert expect.uart_transfer_time(10, 1000) == pytest.approx(0.1)
    assert expect.uart_transfer_time(10, 1100, "odd") == pytest.approx(0.1)


@pytest.mark.parametrize(
    ("timeout_ms", "pclk1", "prescaler", "period"),
    [
        (1, WB55, 1, 4.032e-3),
        (5, WB55, 2, 8.064e-3),
        (20, WB55, 8, 32.256e-3),
        (100, WB55, 32, 129.024e-3),
        (300, WB55, 128, 516.096e-3),
        (516, WB55, 128, 516.096e-3),
        (5, WBA55, 2, 5.16096e-3),
        (20, WBA55, 8, 20.64384e-3),
        (100, WBA55, 64, 165.15072e-3),
        (330, WBA55, 128, 330.30144e-3),
    ],
)
def test_wwdg_prescaler_and_period(timeout_ms, pclk1, prescaler, period):
    assert expect.wwdg_prescaler_for(timeout_ms, pclk1) == prescaler
    assert expect.wwdg_warning_period(timeout_ms, pclk1) == pytest.approx(period)
    assert expect.wwdg_warning_period(timeout_ms, pclk1) >= timeout_ms / 1000


def test_wwdg_limits():
    assert expect.wwdg_max_timeout_ms(WB55) == 516
    assert expect.wwdg_max_timeout_ms(WBA55) == 330
    assert expect.wwdg_prescaler_for(517, WB55) is None
    assert expect.wwdg_prescaler_for(331, WBA55) is None
    with pytest.raises(ValueError):
        expect.wwdg_warning_period(30000, WB55)
    assert expect.wwdg_tick(WB55, 1) == pytest.approx(64e-6)


@pytest.mark.parametrize(
    ("timeout_ms", "pclk1", "margin", "outruns"),
    [
        (5, WB55, 1, False),
        (20, WB55, 1, True),
        (20, WB55, 2, False),
        (100, WB55, 2, True),
        (300, WB55, 2, True),
        (5, WBA55, 1, False),
        (20, WBA55, 1, True),
        (20, WBA55, 2, False),
        (100, WBA55, 2, True),
    ],
)
def test_wwdg_warning_line_against_the_reset(timeout_ms, pclk1, margin, outruns):
    """The line `\\r\\nEVT wdt index=0 warning=1\\r\\n` takes 29 * 10 / 921600 = 314.7 us; the tick before the reset is
    128 us (WB55, 5 ms), 512 us (WB55, 20 ms), 81.9 us (WBA55, 5 ms) or 327.7 us (WBA55, 20 ms)."""
    assert expect.terminal_line_time(expect.wwdg_warning_line(0, 1), 921600) == pytest.approx(314.7e-6, abs=0.1e-6)
    assert expect.wwdg_warning_outruns_reset(timeout_ms, pclk1, 921600, margin) is outruns


def test_adc():
    assert expect.adc_code(0.0) == 0
    assert expect.adc_code(1.65) == 2048
    assert expect.adc_code(3.3) == 4095
    assert expect.adc_code(0.2) == 248
    assert expect.adc_max_runs(1) == 64
    assert expect.adc_max_runs(3) == 21
    assert expect.adc_max_runs(8) == 8
    assert expect.adc_measure_time(32, 1000) == pytest.approx(0.032)
    assert expect.ADC_DEFAULT_SAMPLING["stm32wb55"] in expect.ADC_SAMPLING_TIMES["stm32wb55"]
    assert expect.ADC_DEFAULT_SAMPLING["stm32wba55"] == expect.ADC_SAMPLING_TIMES["stm32wba55"][1]
    assert all(len(times) == 8 for times in expect.ADC_SAMPLING_TIMES.values())


@pytest.mark.parametrize(
    ("cycles", "cap", "direction", "inva", "invb", "counts"),
    [
        (10, "ab", "fwd", 0, 0, 40),
        (10, "ab", "rev", 0, 0, -40),
        (10, "a", "fwd", 0, 0, 20),
        (10, "b", "rev", 0, 0, -20),
        (10, "ab", "fwd", 1, 0, -40),
        (10, "ab", "fwd", 0, 1, -40),
        (10, "ab", "fwd", 1, 1, 40),
        (3, "a", "rev", 1, 0, 6),
    ],
)
def test_qei_counts(cycles, cap, direction, inva, invb, counts):
    assert expect.qei_counts(cycles, cap, direction, inva, invb) == counts


@pytest.mark.parametrize(("delta", "modulus", "wrapped"), [(10, 100, 10), (-10, 100, -10), (95, 100, -5), (50, 100, 50), (-60, 100, 40)])
def test_wrap_delta(delta, modulus, wrapped):
    assert expect.wrap_delta(delta, modulus) == wrapped


def test_wrap_position_and_payload():
    assert expect.wrap_position(50 + 4 * 30, 100) == 70
    assert expect.wrap_position(-4, 4096) == 4092
    assert expect.wrap_position(2**32 + 5, 2**32) == 5
    assert expect.max_hex_payload(255, "uart.send 1") == 121
