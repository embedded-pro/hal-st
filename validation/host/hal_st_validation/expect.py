"""Expected values derived from STM32 hardware behaviour and the hal-st drivers, used by the HIL tests and the
fake firmware (pure functions). Frequencies are in Hz, times in seconds unless the name says otherwise.

Sources: `PwmStmBase::SetBaseFrequencyImpl`/`SetDutyCycle`/`EncodeDeadTime` (hal_st/stm32fxxx/PwmStm.cpp), the
ST HAL `UART_DIV_SAMPLING8`/`UART_DIV_LPUART` macros and `IS_UART_BAUDRATE`, the SPI baud-rate prescaler and the
WWDG counter (validation/PROTOCOL.md and the factories in validation/firmware).
"""

from __future__ import annotations

import math
from typing import Literal

from ad3_waveforms_bench import analysis

PwmMode = Literal["edge", "center"]

# Timer features of STM32WB55/STM32WBA55 (TIM1 advanced; TIM2 32-bit; TIM2/TIM3 general purpose;
# TIM16/TIM17 one channel with complementary output and break).
TIMERS_32BIT = frozenset({2})
BREAK_TIMERS = frozenset({1, 16, 17})
BREAK_FILTER_TIMERS = {"stm32wb55": frozenset({1}), "stm32wba55": frozenset({1, 16, 17})}
SINGLE_CHANNEL_TIMERS = frozenset({16, 17})
ENCODER_TIMERS = frozenset({1, 2, 3})
ADC_TRIGGER_TIMERS = frozenset({1, 2})

PWM_PRESCALER_MAX = 0xFFFF
PWM_DEAD_MAX_NS = 1_000_000
# BDTR.DTG at its largest: (32 + 31) * 16 timer clocks.
PWM_DEAD_MAX_TICKS = 1008


def timer_counter_max(timer: int) -> int:
    return 0xFFFFFFFF if timer in TIMERS_32BIT else 0xFFFF


def timer_has_break(timer: int) -> bool:
    """Complementary outputs, dead time, idle levels and the break input need a timer with a break function."""
    return timer in BREAK_TIMERS


def timer_has_break_filter(family: str, timer: int) -> bool:
    """BDTR.BKF of TIM16/TIM17 reads as zero on the STM32WB55, so `brkfilter` is refused there."""
    return timer in BREAK_FILTER_TIMERS[family]


def timer_has_center_mode(timer: int) -> bool:
    return timer not in SINGLE_CHANNEL_TIMERS


def timer_has_channel(timer: int, channel: int) -> bool:
    """`IS_TIM_CCX_INSTANCE`: TIM16/TIM17 have channel 1 only."""
    return 1 <= channel <= (1 if timer in SINGLE_CHANNEL_TIMERS else 4)


def timer_has_complementary(timer: int, channel: int) -> bool:
    """`IS_TIM_CCXN_INSTANCE` up to CH3N (hal-st has no CH4N pin function): CH1N-CH3N on TIM1, CH1N on TIM16/TIM17."""
    return timer in BREAK_TIMERS and 1 <= channel <= (1 if timer in SINGLE_CHANNEL_TIMERS else 3)


def pwm_clock(timer_clock: int, prescaler: int = 0) -> int:
    """`pwmclk` of `pwm.open`: the counter clock."""
    return timer_clock // (prescaler + 1)


def pwm_ticks(pwmclk: int, frequency: int, mode: PwmMode) -> int:
    """Compare full scale of a period: ARR + 1 edge aligned, ARR centre aligned (the counter runs 0 .. ARR .. 0,
    2 * ARR ticks per period)."""
    ticks = pwmclk // frequency
    return ticks // 2 if mode == "center" else ticks


def pwm_auto_reload(pwmclk: int, frequency: int, mode: PwmMode) -> int:
    """ARR `PwmStm` writes for `frequency`."""
    ticks = pwmclk // frequency
    return ticks // 2 if mode == "center" else ticks - 1


def pwm_fits(pwmclk: int, frequency: int, mode: PwmMode, counter_max: int = 0xFFFF) -> bool:
    """`pwm.open`/`pwm.freq` answer `ERR range` otherwise: at least 2 counter ticks per period edge aligned and 4
    centre aligned (ARR >= 2: at ARR = 1 the output is a fixed half period), and ARR within the counter."""
    if frequency <= 0:
        return False
    return pwmclk // frequency >= pwm_minimum_ticks(mode) and pwm_auto_reload(pwmclk, frequency, mode) <= counter_max


def pwm_minimum_ticks(mode: PwmMode) -> int:
    return 4 if mode == "center" else 2


def pwm_frequency(pwmclk: int, frequency: int, mode: PwmMode) -> float:
    """Output frequency the protocol asks for after quantisation to whole counter ticks: `ticks` per period edge
    aligned, `2 * ticks` centre aligned (the counter runs 0 .. ARR .. 0 with ARR = ticks)."""
    ticks = pwm_ticks(pwmclk, frequency, mode)
    return pwmclk / (2 * ticks if mode == "center" else ticks)


def pwm_frequency_limits(pwmclk: int, mode: PwmMode, counter_max: int = 0xFFFF) -> tuple[int, int]:
    """Lowest and highest frequency `pwm.open`/`pwm.freq` accept at `pwmclk` (at least `pwm_minimum_ticks`, ARR
    within the counter); the accepted frequencies form one interval."""
    longest = 2 * counter_max + 1 if mode == "center" else counter_max + 1
    lowest = max(1, pwmclk // (longest + 1) + 1)
    highest = pwmclk // pwm_minimum_ticks(mode)
    while lowest > 1 and pwm_fits(pwmclk, lowest - 1, mode, counter_max):
        lowest -= 1
    while not pwm_fits(pwmclk, lowest, mode, counter_max) and lowest <= highest:
        lowest += 1
    if lowest > highest:
        raise ValueError(f"no frequency fits pwmclk {pwmclk} Hz ({mode})")
    return lowest, highest


def pwm_duty_counts(pwmclk: int, frequency: int, mode: PwmMode, duty: float) -> int:
    """CCR for a duty in percent (`DutyCycle::ToCounts` rounds to nearest over the ticks of a period)."""
    return math.floor(pwm_ticks(pwmclk, frequency, mode) * duty / 100 + 0.5)


def pwm_duty(pwmclk: int, frequency: int, mode: PwmMode, duty: float) -> float:
    """Duty in percent after rounding to whole counts (PWM mode 1, active while CNT < CCR)."""
    full = pwm_ticks(pwmclk, frequency, mode)
    return 100 * min(pwm_duty_counts(pwmclk, frequency, mode, duty), full) / full


def pwm_duty_step(pwmclk: int, frequency: int, mode: PwmMode) -> float:
    """Duty resolution in percent: one compare count."""
    return 100 / pwm_ticks(pwmclk, frequency, mode)


def pwm_duty_resolvable(pwmclk: int, frequency: int, mode: PwmMode, duty: float) -> bool:
    """Whether a duty between 0 and 100 % keeps both edges after rounding to whole counts."""
    return 0 < pwm_duty_counts(pwmclk, frequency, mode, duty) < pwm_ticks(pwmclk, frequency, mode)


def pwm_prescaler_for(timer_clock: int, frequency: int, mode: PwmMode, counter_max: int = 0xFFFF) -> int:
    """Smallest prescaler whose counter holds `frequency`."""
    for prescaler in range(PWM_PRESCALER_MAX + 1):
        pwmclk = pwm_clock(timer_clock, prescaler)
        if pwm_fits(pwmclk, frequency, mode, counter_max):
            return prescaler
        if pwmclk // frequency < 2:
            break
    raise ValueError(f"{frequency} Hz does not fit any prescaler at {timer_clock} Hz ({mode})")


def pwm_dead_fits(dead_ns: int) -> bool:
    """`dead` above 1 ms is `ERR range`; longer than the DTG field can encode saturates instead."""
    return 0 <= dead_ns <= PWM_DEAD_MAX_NS


def pwm_dead_ticks(dead_ns: int, timer_clock: int) -> int:
    """Timer clocks of dead time the DTG field encodes for `dead_ns`, rounded up (`EncodeDeadTime`)."""
    if dead_ns <= 0 or timer_clock <= 0:
        return 0
    if dead_ns > PWM_DEAD_MAX_TICKS * 1_000_000_000 // timer_clock:
        return PWM_DEAD_MAX_TICKS
    ticks = -(-dead_ns * timer_clock // 1_000_000_000)
    for limit, step in ((127, 1), (254, 2), (504, 8), (PWM_DEAD_MAX_TICKS, 16)):
        if ticks <= limit:
            return -(-ticks // step) * step
    return PWM_DEAD_MAX_TICKS


def pwm_dead_time(dead_ns: int, timer_clock: int) -> float:
    """Dead time the timer inserts, in seconds."""
    return pwm_dead_ticks(dead_ns, timer_clock) / timer_clock


SPI_DIVIDERS = tuple(2**n for n in range(1, 9))


def spi_baud_fits(spiclk: int, baud: int) -> bool:
    """`spi.open` answers `ERR range` for `baud` outside spiclk/256 .. spiclk/2."""
    return spiclk <= 256 * baud and 2 * baud <= spiclk


def spi_divider(spiclk: int, baud: int) -> int:
    """The baud-rate prescaler: the fastest spiclk / 2^n (n = 1-8) not above `baud`."""
    if not spi_baud_fits(spiclk, baud):
        raise ValueError(f"{baud} Hz is outside {spiclk}/256 .. {spiclk}/2")
    return next(divider for divider in SPI_DIVIDERS if spiclk <= baud * divider)


def spi_clock(spiclk: int, baud: int) -> float:
    """SCK frequency `spi.open baud=<baud>` produces."""
    return spiclk / spi_divider(spiclk, baud)


UART_BAUD_MIN = 300
UART_BAUD_MAX = 12_000_000
# `IS_UART_BAUDRATE` of the ST HAL, which `HAL_UART_Init` asserts (the firmware is built with USE_FULL_ASSERT).
UART_HAL_BAUD_MAX = {"stm32wb55": 8_000_000, "stm32wba55": 12_499_999}
USART_BRR_MIN = 16
USART_BRR_MAX = 0xFFFF
LPUART_BRR_MIN = 0x300
LPUART_BRR_MAX = 0xFFFFF


def uart_clock_name(index: int, lp: bool = False) -> str:
    """Key of the board file's `clocks.uart` table: `usart2`, `lpuart1`."""
    return f"{'lpuart' if lp else 'usart'}{index}"


def uart_baud_max(family: str) -> int:
    """Highest `baud` of `uart.open` on `family`: 12000000, or the lower limit the HAL asserts (8000000 on the
    STM32WB55)."""
    return min(UART_BAUD_MAX, UART_HAL_BAUD_MAX.get(family, UART_BAUD_MAX))


def uart_divider(clock: int, baud: int, lp: bool = False) -> int:
    """BRR as the ST HAL computes it: `UART_DIV_SAMPLING8` on a USART, `UART_DIV_LPUART` (256 * clock / baud) on an
    LPUART, both rounded to nearest."""
    return ((256 if lp else 2) * clock + baud // 2) // baud


def uart_baud_fits(clock: int, baud: int, lp: bool = False, maximum: int = UART_BAUD_MAX) -> bool:
    """`uart.open` answers `ERR range` otherwise: `baud` within 300 .. `maximum` (`uart_baud_max`) and the divider
    within the baud-rate register (`UartFactory.cpp` `BaudRateFits`)."""
    if not UART_BAUD_MIN <= baud <= maximum:
        return False
    divider = uart_divider(clock, baud, lp)
    if lp:
        return LPUART_BRR_MIN <= divider <= LPUART_BRR_MAX
    return USART_BRR_MIN <= divider <= USART_BRR_MAX


def uart_baud_limits(clock: int, lp: bool = False, maximum: int = UART_BAUD_MAX) -> tuple[int, int]:
    """Lowest and highest baud rate `uart.open` accepts at `clock` (the accepted rates form one interval)."""
    probe = next((baud for baud in (115200, 921600, 9600, 3_000_000) if uart_baud_fits(clock, baud, lp, maximum)), None)
    if probe is None:
        raise ValueError(f"no baud rate fits {clock} Hz")
    low, high = UART_BAUD_MIN, probe
    while low < high:
        middle = (low + high) // 2
        low, high = (low, middle) if uart_baud_fits(clock, middle, lp, maximum) else (middle + 1, high)
    lowest = low
    low, high = probe, maximum
    while low < high:
        middle = (low + high + 1) // 2
        low, high = (middle, high) if uart_baud_fits(clock, middle, lp, maximum) else (low, middle - 1)
    return lowest, low


def uart_actual_baud(clock: int, baud: int, lp: bool = False) -> float:
    return (256 if lp else 2) * clock / uart_divider(clock, baud, lp)


def uart_frame_bits(parity: str, stop_bits: int = 1, data_bits: int = 8) -> int:
    return 1 + data_bits + (parity != "none") + stop_bits


def uart_transfer_time(size: int, baud: int, parity: str = "none", stop_bits: int = 1) -> float:
    return size * uart_frame_bits(parity, stop_bits) / baud


WWDG_PRESCALERS = (1, 2, 4, 8, 16, 32, 64, 128)
# Counter ticks from a refresh (0x7F) to the early warning (0x40).
WWDG_WARNING_TICKS = 63
WDT_TIMEOUT_MAX_MS = 30000


def wwdg_tick(pclk1: int, prescaler: int) -> float:
    return 4096 * prescaler / pclk1


def wwdg_period(pclk1: int, prescaler: int) -> float:
    """Early-warning period: 63 * 4096 * prescaler / PCLK1."""
    return WWDG_WARNING_TICKS * wwdg_tick(pclk1, prescaler)


def wwdg_prescaler_for(timeout_ms: int, pclk1: int) -> int | None:
    """Smallest prescaler whose early-warning period is at least `timeout_ms`; None means `ERR range`."""
    for prescaler in WWDG_PRESCALERS:
        if WWDG_WARNING_TICKS * 4096 * prescaler * 1000 >= timeout_ms * pclk1:
            return prescaler
    return None


def wwdg_warning_period(timeout_ms: int, pclk1: int) -> float:
    """Period of `EVT wdt` (and of the `pin=` toggle) for `wdt.start timeout=<timeout_ms>`."""
    prescaler = wwdg_prescaler_for(timeout_ms, pclk1)
    if prescaler is None:
        raise ValueError(f"{timeout_ms} ms is beyond the WWDG at {pclk1} Hz")
    return wwdg_period(pclk1, prescaler)


def wwdg_max_timeout_ms(pclk1: int) -> int:
    """Largest `timeout` `wdt.start` accepts (516 ms at 64 MHz, 330 ms at 100 MHz)."""
    return WWDG_WARNING_TICKS * 4096 * WWDG_PRESCALERS[-1] * 1000 // pclk1


def terminal_line_time(line: str, baud: int) -> float:
    """Time the terminal (8N1) takes to send `line` with the `\\r\\n` before and after it."""
    return uart_transfer_time(len(line) + 4, baud)


def wwdg_warning_line(index: int, warnings: int) -> str:
    return f"EVT wdt index={index} warning={warnings}"


def wwdg_warning_outruns_reset(timeout_ms: int, pclk1: int, baud: int, margin: float = 1.0) -> bool:
    """Whether the `EVT wdt` line of an early warning nobody answers (`feed=manual`) reaches the host: the WWDG resets
    one counter tick after the warning, while the interrupt only schedules the line, so that tick has to outlast the
    line's transmission `margin` times (room for the event loop's latency). False for the shortest timeouts (5 ms on
    both boards: a tick of 128 us at 64 MHz, 82 us at 100 MHz against 315 us for the line at 921600 Bd)."""
    prescaler = wwdg_prescaler_for(timeout_ms, pclk1)
    if prescaler is None:
        raise ValueError(f"{timeout_ms} ms is beyond the WWDG at {pclk1} Hz")
    return wwdg_tick(pclk1, prescaler) > margin * terminal_line_time(wwdg_warning_line(0, 1), baud)


ADC_SAMPLING_TIMES = {
    "stm32wb55": ("2.5", "6.5", "12.5", "24.5", "47.5", "92.5", "247.5", "640.5"),
    "stm32wba55": ("1.5", "3.5", "7.5", "12.5", "19.5", "39.5", "79.5", "814.5"),
}
ADC_DEFAULT_SAMPLING = {"stm32wb55": "2.5", "stm32wba55": "3.5"}
ADC_MAX_PINS = 8
ADC_MAX_VALUES = 64
ADC_RATE_MAX = 100_000
ADC_DEFAULT_RATE = 1000


def adc_code(volts: float, vref: float = 3.3, bits: int = 12) -> int:
    """Ideal raw code for `volts` (1 LSB = vref / 2^bits)."""
    return analysis.adc_code(volts, vref, bits)


def adc_max_runs(pins: int) -> int:
    """Largest `adc.measure n=` for a sequence of `pins` conversions (at most 64 values)."""
    return ADC_MAX_VALUES // pins


def adc_measure_time(runs: int, rate: int) -> float:
    """Time a timer-triggered `adc.measure n=<runs>` needs at `rate` runs per second: the measurement starts the
    timer, whose first trigger comes one period later."""
    return runs / rate


QEI_COUNTS_PER_CYCLE = {"ab": 4, "a": 2, "b": 2}


def qei_counts(cycles: int, cap: str = "ab", direction: str = "fwd", inva: bool = False, invb: bool = False) -> int:
    """Signed count change for `cycles` quadrature cycles; inverting exactly one phase reverses the direction."""
    forward = (direction == "fwd") != (bool(inva) != bool(invb))
    return QEI_COUNTS_PER_CYCLE[cap] * cycles * (1 if forward else -1)


def wrap_delta(delta: int, modulus: int) -> int:
    """Signed difference of two positions on a counter that wraps at `modulus`, in (-modulus/2, modulus/2]."""
    delta %= modulus
    return delta - modulus if delta > modulus // 2 else delta


def wrap_position(position: int, modulus: int) -> int:
    """Position on a counter that wraps at `modulus` (`res`)."""
    return position % modulus


def max_hex_payload(max_command_length: int, command_prefix: str) -> int:
    """Largest byte count whose hex encoding still fits the firmware's command line."""
    return max(0, (max_command_length - len(command_prefix) - 1) // 2)
