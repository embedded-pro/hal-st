"""Offline double of the validation firmware: `FakeFirmware` answers the validation/PROTOCOL.md commands behind
the generic `FakeTerminalDevice` (echo, prompt, `EVT` lines); connect it with `FakeSerial`.

It validates arguments like EMIL's command groups and the hal-st factories of validation/firmware, in their
order (`usage`, `range`, `pin`, `unsupported` before `busy`, `notopen`), and models what needs no hardware: the
board profiles (ports, bonded pins, reserved pins, instances, the pin functions of the generated pinout tables,
analog pins), pin ownership, the one-instance-per-group limits, timers shared between PWM, encoder and
timer-triggered ADC, the peripherals and DMA channels groups share (`ResourceAllocation`), EXTI line ownership,
ADC trigger timing and WWDG warnings and resets over a (fake) clock.
It models the protocol, not the driver gaps of the board files' `known_gaps`.
Measured signals (DIO levels, analog codes, UART/SPI peers, encoder counts) are not emulated; tests preset
`gpio_levels`, `adc_codes`, `uart_rx` and `spi_miso` instead.

The groups of `fakes/*.py` (`FakeGroup` subclasses) are found at construction and answer `<prefix>.<verb>`
before the groups modelled here; they share the pin claims, timer owners and resources through the methods
listed in `fakes/base.py`.
"""

from __future__ import annotations

import math
import re
import time
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass, field
from typing import Any

from ad3_waveforms_bench.fake_terminal import FakeSerial as _FakeSerial
from ad3_waveforms_bench.fake_terminal import FakeTerminalDevice, Handler
from ad3_waveforms_bench.protocol import format_hex

from . import expect
from .fakes import FakeGroup
from .fakes import discover as discover_groups
from .fakes.base import UINT32_MAX, FakeError, _choice, _fail, _flag, _hex, _number, _shape, _tokens
from .fakes.base import _Error as _Error

__all__ = ["FakeFirmware", "FakeSerial", "OPEN_LIMITS", "RESOURCES", "UNSUPPORTED_COMMANDS", "WB55_PINS", "WBA55_PINS"]

Owner = tuple[str, str]

# The alias tables of PROTOCOL.md, in the order `board.pins` prints them.
WB55_PINS: dict[str, str] = {
    "terminaltx": "PB6",
    "terminalrx": "PB7",
    "ain1": "PC0",
    "ain2": "PC1",
    "ain3": "PC2",
    "ain4": "PC3",
    "ain5": "PA0",
    "ain6": "PA1",
    "tim1ch1": "PA8",
    "tim1ch2": "PA9",
    "tim1ch3": "PA10",
    "tim1ch4": "PA11",
    "tim1ch1n": "PA7",
    "tim1ch2n": "PB8",
    "tim1ch3n": "PB9",
    "tim1bkin": "PB12",
    "tim2ch1": "PA15",
    "tim2ch2": "PA1",
    "tim2ch3": "PA2",
    "tim2ch4": "PA3",
    "tim16ch1": "PA6",
    "tim17ch1": "PB9",
    "qei1a": "PA8",
    "qei1b": "PA9",
    "qei2a": "PA15",
    "qei2b": "PA1",
    "qei2idx": "PC6",
    "lptim1in1": "PC0",
    "lptim1in2": "PC2",
    "spi1clk": "PA5",
    "spi1miso": "PA6",
    "spi1mosi": "PA7",
    "spi1cs": "PA4",
    "lpuart1tx": "PA2",
    "lpuart1rx": "PA3",
    "lpuart1rts": "PB12",
    "lpuart1cts": "PA6",
    "led0": "PB0",
    "led1": "PB1",
    "gpio0": "PC6",
    "gpio1": "PC10",
    "gpio2": "PC12",
    "gpio3": "PC13",
    "gpio4": "PE4",
    "sw1": "PC4",
    "sw2": "PD0",
    "sw3": "PD1",
    "i2c1scl": "PB8",
    "i2c1sda": "PB9",
    "i2c3scl": "PC0",
    "i2c3sda": "PC1",
    "spi2clk": "PB13",
    "spi2miso": "PB14",
    "spi2mosi": "PB15",
    "spi2nss": "PB12",
    "spi1nss": "PA4",
    "qspiclk": "PA3",
    "qspincs": "PA2",
    "qspiio0": "PB9",
    "qspiio1": "PB8",
    "qspiio2": "PA7",
    "qspiio3": "PA6",
    "mco": "PA8",
}

WBA55_PINS: dict[str, str] = {
    "terminaltx": "PB12",
    "terminalrx": "PA8",
    "ain2": "PA7",
    "ain3": "PA6",
    "ain4": "PA5",
    "ain7": "PA2",
    "ain8": "PA1",
    "ain9": "PA0",
    "ain10": "PB9",
    "tim1ch1": "PA11",
    "tim1ch2": "PA12",
    "tim1ch3": "PB4",
    "tim1ch4": "PB3",
    "tim1ch1n": "PB2",
    "tim1ch2n": "PB1",
    "tim1ch3n": "PB0",
    "tim1bkin": "PA2",
    "tim2ch1": "PA5",
    "tim2ch3": "PA7",
    "tim2ch4": "PA6",
    "tim3ch1": "PA10",
    "tim3ch2": "PA1",
    "tim3ch3": "PB14",
    "tim3ch4": "PB9",
    "tim16ch1": "PB9",
    "tim17ch1": "PA1",
    "tim17ch1n": "PB3",
    "qei1a": "PA11",
    "qei1b": "PA12",
    "qei1idx": "PA15",
    "qei3a": "PA10",
    "qei3b": "PA1",
    "spi1clk": "PB4",
    "spi1miso": "PB3",
    "spi1mosi": "PA15",
    "spi1cs": "PA12",
    "lpuart1tx": "PB5",
    "lpuart1rx": "PA10",
    "lpuart1rts": "PB9",
    "lpuart1cts": "PB15",
    "usart2tx": "PB0",
    "usart2rx": "PA11",
    "usart2rts": "PB1",
    "usart2cts": "PB2",
    "led0": "PB4",
    "led1": "PA9",
    "gpio0": "PB14",
    "gpio1": "PA5",
    "gpio2": "PA0",
    "sw1": "PC13",
    "sw2": "PB6",
    "sw3": "PB7",
    "i2c1scl": "PB2",
    "i2c1sda": "PB1",
    "i2c3scl": "PA6",
    "i2c3sda": "PA7",
    "spi3clk": "PA0",
    "spi3miso": "PB9",
    "spi3mosi": "PB8",
    "spi3nss": "PA5",
    "spi1nss": "PA12",
    "lptim1ch2": "PA15",
    "lptim2ch1": "PA11",
    "lptim2ch2": "PA1",
    "lptim1in1": "PA0",
    "lptim1in2": "PB3",
    "lptim2in1": "PB9",
    "lptim2in2": "PB0",
    "tim16ch1n": "PB8",
}

_UNSUPPORTED_EVERYWHERE = (
    "comp.open",
    "comp.read",
    "comp.irq",
    "comp.count",
    "comp.close",
    "can.open",
    "can.send",
    "can.close",
    "eth.open",
    "eth.status",
    "eth.close",
)

# `HilUnsupportedCommands` of validation/firmware/UnsupportedGroups.cpp: groups hal-st has no driver for on the MCU.
UNSUPPORTED_COMMANDS: dict[str, tuple[str, ...]] = {
    "stm32wb55": (
        *_UNSUPPORTED_EVERYWHERE,
        "lptpwm.open",
        "lptpwm.duty",
        "lptpwm.pulse",
        "lptpwm.start",
        "lptpwm.stop",
        "lptpwm.close",
    ),
    "stm32wba55": (
        *_UNSUPPORTED_EVERYWHERE,
        "hsem.take",
        "hsem.release",
        "hsem.status",
        "hsem.lock",
        "hsem.mine",
        "qspi.open",
        "qspi.cmd",
        "qspi.poll",
        "qspi.xfer",
        "qspi.close",
        "flash.stack",
        "clock.mco",
        "clock.hsi48",
    ),
}

# `ResourceAllocation`: one owner per peripheral or shared DMA channel across groups.
RESOURCES = ("lpTimer", "spi", "i2c", "adc", "dma1", "dma2", "hsem")
_RESOURCE_INDEX_LIMIT = 16

Table = dict[int, tuple[str, ...]]


def _table(**instances: str) -> Table:
    """`_table(i1="PA9 PB6")` → `{1: ("PA9", "PB6")}`."""
    return {int(name[1:]): tuple(pins.split()) for name, pins in instances.items()}


# Pin functions of the generated pinout tables (`PinoutTableDefault.cpp` of build/stm32wb55 and build/stm32wba55),
# one-based instances; QUADSPI uses instance 0, as `QuadSpiStm` claims its pins. The tables also list pins of
# larger packages; `bonded` filters them like the firmware does.
_WB55_FUNCTIONS: dict[str, Table] = {
    "uartTx": _table(i1="PA9 PB6"),
    "uartRx": _table(i1="PA10 PB7"),
    "uartRts": _table(i1="PA12 PB3"),
    "uartCts": _table(i1="PA11 PB4"),
    "lpuartTx": _table(i1="PA2 PB5 PB11 PC1"),
    "lpuartRx": _table(i1="PA3 PA12 PB10 PC0"),
    "lpuartRts": _table(i1="PB1 PB12"),
    "lpuartCts": _table(i1="PA6 PB13"),
    "spiClock": _table(i1="PA1 PA5 PB3", i2="PA9 PB10 PB13 PD1 PD3"),
    "spiMiso": _table(i1="PA6 PA11 PB4", i2="PB14 PC2 PD3"),
    "spiMosi": _table(i1="PA7 PA12 PB5", i2="PB15 PC1 PC3 PD4"),
    "spiSlaveSelect": _table(i1="PA4 PA15 PB2", i2="PB9 PB12 PD0"),
    "i2cScl": _table(i1="PA9 PB6 PB8", i3="PA7 PB10 PB13 PC0"),
    "i2cSda": _table(i1="PA10 PB7 PB9", i3="PB4 PB11 PB14 PC1"),
    "timerChannel1": _table(i1="PA8 PD14", i2="PA0 PA5 PA15", i16="PA6 PB8 PE0", i17="PA7 PB9 PE1"),
    "timerChannel2": _table(i1="PA9 PD15", i2="PA1 PB3"),
    "timerChannel3": _table(i1="PA10", i2="PA2 PB10"),
    "timerChannel4": _table(i1="PA11", i2="PA3 PB11"),
    "timerChannel1N": _table(i1="PA7 PB13", i16="PB6", i17="PB7"),
    "timerChannel2N": _table(i1="PB8 PB14"),
    "timerChannel3N": _table(i1="PB9 PB15"),
    "timerBreak": _table(i1="PA6 PB7 PB12 PC9", i16="PB5", i17="PA10 PB4"),
    "lpTimerChannel1": {},
    "lpTimerChannel2": {},
    "lpTimerInput1": _table(i1="PB5 PC0", i2="PB1 PC0 PD12"),
    "lpTimerInput2": _table(i1="PB7 PC2"),
    "quadSpiClock": _table(i0="PA3 PB10"),
    "quadSpiSlaveSelect": _table(i0="PA2 PB11 PD3"),
    "quadSpiData0": _table(i0="PB9 PD4"),
    "quadSpiData1": _table(i0="PB8 PD5"),
    "quadSpiData2": _table(i0="PA7 PD6"),
    "quadSpiData3": _table(i0="PA6 PD7"),
}

_WBA55_FUNCTIONS: dict[str, Table] = {
    "uartTx": _table(i1="PB12 PB14", i2="PA12 PA14 PB0"),
    "uartRx": _table(i1="PA8", i2="PA11 PB4 PB8"),
    "uartRts": _table(i1="PA2 PA3 PA6", i2="PA15 PB1"),
    "uartCts": _table(i1="PA7", i2="PB2 PB15"),
    "lpuartTx": _table(i1="PA2 PB5 PB11"),
    "lpuartRx": _table(i1="PA1 PA10"),
    "lpuartRts": _table(i1="PA9 PB9"),
    "lpuartCts": _table(i1="PA0 PB15"),
    "spiClock": _table(i1="PB4", i3="PA0"),
    "spiMiso": _table(i1="PB3", i3="PB9"),
    "spiMosi": _table(i1="PA15", i3="PB8"),
    "spiSlaveSelect": _table(i1="PA12", i3="PA5"),
    "i2cScl": _table(i1="PA15 PB2", i3="PA6 PB2"),
    "i2cSda": _table(i1="PB1 PB3", i3="PA7 PB1"),
    "timerChannel1": _table(i1="PA11 PB8", i2="PA5 PB6 PB12", i3="PA2 PA10 PB5", i16="PA2 PB9", i17="PA1 PB4"),
    "timerChannel2": _table(i1="PA12", i2="PA8", i3="PA1 PA9"),
    "timerChannel3": _table(i1="PB4", i2="PA7", i3="PA0 PB14"),
    "timerChannel4": _table(i1="PB3", i2="PA6", i3="PB9 PB13"),
    "timerChannel1N": _table(i1="PA1 PB2", i16="PA3 PB8", i17="PB3"),
    "timerChannel2N": _table(i1="PA0 PB1"),
    "timerChannel3N": _table(i1="PB0 PB9"),
    "timerBreak": _table(i1="PA2", i16="PB10 PB15", i17="PA15"),
    "lpTimerChannel1": _table(i1="PB11", i2="PA11"),
    "lpTimerChannel2": _table(i1="PA8 PA15", i2="PA1"),
    "lpTimerInput1": _table(i1="PA0", i2="PB9"),
    "lpTimerInput2": _table(i1="PB3", i2="PB0 PB4"),
}


@dataclass(frozen=True)
class _Family:
    family: str
    board: str
    sysclk: int
    ports: str
    bonded: dict[str, int]
    pins: dict[str, str]
    debug_led: str
    reserved: tuple[str, ...]
    usarts: frozenset[int]
    lpuarts: frozenset[int]
    # (lp, index) of the UARTs with DMA requests (`board::UartDma`).
    uart_dma: frozenset[tuple[bool, int]]
    # `SynchronousUartStmSendOnly` has LPUART constructors on STM32WB only.
    lpuart_send_only: bool
    spis: frozenset[int]
    # `IS_SPI_LIMITED_INSTANCE`: 8- and 16-bit frames only.
    spi_limited: frozenset[int]
    i2cs: frozenset[int]
    qspi: frozenset[int]
    timers: frozenset[int]
    lptims: frozenset[int]
    # `IS_LPTIM_ENCODER_INTERFACE_INSTANCE`
    lptim_encoders: frozenset[int]
    adc: int
    default_lpuart: tuple[str, str]
    default_qei: tuple[int, str, str, str]
    functions: dict[str, Table]
    analog: dict[str, int]

    @property
    def sampling(self) -> tuple[str, ...]:
        return expect.ADC_SAMPLING_TIMES[self.family]

    @property
    def unsupported(self) -> tuple[str, ...]:
        return UNSUPPORTED_COMMANDS[self.family]


_FAMILIES: dict[str, _Family] = {
    "stm32wb55": _Family(
        family="stm32wb55",
        board="NUCLEO-WB55RG",
        sysclk=64_000_000,
        ports="ABCDEH",
        bonded={"A": 0xFFFF, "B": 0xFFFF, "C": 0xFC7F, "D": 0x0003, "E": 0x0010, "H": 0x0008},
        pins=WB55_PINS,
        debug_led="PB5",
        reserved=("PA13", "PA14", "PC14", "PC15", "PH3"),
        usarts=frozenset({1}),
        lpuarts=frozenset({1}),
        uart_dma=frozenset({(False, 1), (True, 1)}),
        lpuart_send_only=True,
        spis=frozenset({1, 2}),
        spi_limited=frozenset(),
        i2cs=frozenset({1, 3}),
        qspi=frozenset({1}),
        timers=frozenset({1, 2, 16, 17}),
        lptims=frozenset({1, 2}),
        lptim_encoders=frozenset({1}),
        adc=1,
        default_lpuart=("PA2", "PA3"),
        default_qei=(2, "PA15", "PA1", "PC6"),
        functions=_WB55_FUNCTIONS,
        analog={
            **{"PA0": 5, "PA1": 6, "PA2": 7, "PA3": 8, "PA4": 9, "PA5": 10, "PA6": 11, "PA7": 12, "PA8": 15, "PA9": 16},
            **{"PC0": 1, "PC1": 2, "PC2": 3, "PC3": 4, "PC4": 13, "PC5": 14},
        },
    ),
    # PA3, PB10, PB11 and PB13 are SMPS and core supply pins on the UFQFPN48; hal-st builds the WBA55 from the
    # WBA52 description, whose tables still list them.
    "stm32wba55": _Family(
        family="stm32wba55",
        board="NUCLEO-WBA55CG",
        sysclk=100_000_000,
        ports="ABCH",
        bonded={"A": 0xFFE7, "B": 0xD3FF, "C": 0xE000, "H": 0x0008},
        pins=WBA55_PINS,
        debug_led="PA9",
        reserved=("PA13", "PA14", "PC14", "PC15", "PH3"),
        usarts=frozenset({1, 2}),
        lpuarts=frozenset({1}),
        uart_dma=frozenset({(False, 1), (False, 2), (True, 1)}),
        lpuart_send_only=False,
        spis=frozenset({1, 3}),
        spi_limited=frozenset({3}),
        i2cs=frozenset({1, 3}),
        qspi=frozenset(),
        timers=frozenset({1, 2, 3, 16, 17}),
        lptims=frozenset({1, 2}),
        lptim_encoders=frozenset({1, 2}),
        adc=4,
        default_lpuart=("PB5", "PA10"),
        default_qei=(1, "PA11", "PA12", "PA15"),
        functions=_WBA55_FUNCTIONS,
        analog={"PA0": 9, "PA1": 8, "PA2": 7, "PA3": 6, "PA5": 4, "PA6": 3, "PA7": 2, "PA8": 1, "PB9": 10},
    ),
}

_TERMINAL_USART = 1
_DEFAULT_LPUART = 1
# `Instances()` of the factories: instance numbers 0 .. n-1 parse, the others answer `ERR range`.
_INSTANCES = {"uart": 3, "spi": 4, "pwm": 18, "qei": 18, "wdt": 1}
# `hal::peripheralTimer` holds TIM1 .. TIM17.
_TIMER_TABLE_SIZE = 17
_ADC_KEY_MAX = 0xFFFF
# Instances a group holds at once (PROTOCOL.md: one per group, eight GPIO pins); groups not listed hold one.
OPEN_LIMITS = {
    "pwm": 1,
    "uart": 1,
    "spi": 1,
    "adc": 1,
    "qei": 1,
    "gpio": 8,
    "i2c": 1,
    "i2cs": 1,
    "eeprom": 1,
    "spis": 1,
    "tim": 1,
    "tpwm": 1,
    "lptim": 1,
    "lptpwm": 1,
    "qspi": 1,
}
_PWM_CHANNELS_MAX = 4
_PWM_MODES = ("edge", "edgedown", "center", "centerup", "centerboth")
_PWM_TRIGGER_OUTPUTS = ("reset", "enable", "update", "oc1", "oc1ref", "oc2ref", "oc3ref", "oc4ref")
_PWM_BREAK_FILTER_MAX = 15
_UART_RECEIVE_CAPACITY = 256
_UART_TRANSMIT_CAPACITY = 112
_SPI_CAPACITY = 64
_SPI_BITS = (4, 16)
_SPI_LIMITED_BITS = (8, 16)
_DELAY_MAX_MS = 600_000
_PULSES_MAX = 1_000_000
_PULSE_PERIOD_MAX_MS = 60_000
_RECEIVE_TIMEOUT_MAX_MS = 10_000
_ADC_MEASURE_TIMEOUT = 1.0
_ADC_DMA = ("dma1", 7)
# `adc.open trgo=`: PwmStm drives TRGO only, and the WBA55 ADC4 reaches TIM1 through TRGO2 alone (AdcFactory.cpp).
_ADC_PWM_TRIGGER_TIMERS = {"stm32wb55": frozenset({1, 2}), "stm32wba55": frozenset({2})}
_QEI_RESOLUTION_16BIT = 65536
_QEI_VELOCITY_MAX_US = 1_000_000
_QEI_CAPTURES = ("a", "b", "ab")
_QEI_LP_CAPTURES = ("ab", "rise", "fall")
_LPTIM_FILTERS = (0, 2, 4, 8)
# `HilPinNamingDefault`: a port letter and up to three digits without leading zero.
_PIN_RE = re.compile(r"^[Pp]([A-Za-z])(0|[1-9][0-9]{0,2})$")


class FakeSerial(_FakeSerial):
    """`FakeSerial` that lets the device produce time-driven output (watchdog warnings and resets) on reads."""

    @property
    def in_waiting(self) -> int:
        self._poll()
        return super().in_waiting

    def read(self, size: int = 1) -> bytes:
        self._poll()
        return super().read(size)

    def write(self, data: bytes) -> int:
        self._poll()
        return super().write(data)

    def _poll(self) -> None:
        poll = getattr(self.device, "poll", None)
        if poll is not None:
            poll()


def _duty(text: str) -> float:
    """`HilArguments::ParseDutyCycle`: decimal percent 0-100 with up to 4 decimals."""
    integer, dot, fraction = text.partition(".")
    if not integer.isdigit() or (dot and not (1 <= len(fraction) <= 4 and fraction.isdigit())):
        _fail("usage")
    value = float(text)
    if value > 100:
        _fail("usage")
    return value


def _pwm_alignment(mode: str) -> expect.PwmMode:
    """`center`, `centerup` and `centerboth` count up and down; `edge` and `edgedown` one way."""
    return "center" if mode.startswith("center") else "edge"


@dataclass
class _PwmOutput:
    """One entry of `pwm.open`: channel 0 until it is inferred from the pin."""

    channel: int
    pin: str | None = None
    npin: str | None = None


@dataclass
class FakeFirmware(FakeTerminalDevice):
    """Emulates the validation firmware behind `services::HilTerminal`; `style`, `noise` and `emit()` come from
    `FakeTerminalDevice`. `board`, `sysclk` and `pins` default to the profile of `family` (every kernel clock
    equals `sysclk`, as every APB prescaler is 1).

    The argument checks follow the order of `validation/firmware/*Factory.cpp` and EMIL's command groups, so the
    first error of a command line is the one the firmware reports."""

    board: str | None = None
    family: str = "stm32wb55"
    sysclk: int | None = None
    pins: dict[str, str] | None = None
    unrecognized: str = "ERR usage"
    uid: str = "0123456789abcdef01234567"
    gpio_levels: dict[str, int] = field(default_factory=dict)
    gpio_counts: dict[str, int] = field(default_factory=dict)
    adc_codes: dict[str, int] = field(default_factory=dict)
    uart_rx: dict[int, bytearray] = field(default_factory=dict)
    spi_miso: int = 0x00
    opened: dict[tuple[str, str], dict[str, Any]] = field(default_factory=dict)
    reset_cause: str = "pin"
    # The terminal's line rate: what the reset leaves of a watchdog warning, the time an `adc.measure` reply takes.
    terminal_baud: int = 921600
    clock: Callable[[], float] = time.monotonic
    sleep: Callable[[float], None] = time.sleep

    def __post_init__(self) -> None:
        if self.family not in _FAMILIES:
            raise ValueError(f"unknown family {self.family!r}")
        spec = _FAMILIES[self.family]
        self.board = self.board or spec.board
        self.sysclk = self.sysclk or spec.sysclk
        if self.pins is None:
            self.pins = dict(spec.pins)
        self.claims: dict[str, tuple[Owner, bool]] = {}
        self.timer_owners: dict[int, Owner] = {}
        self.resources: dict[tuple[str, int], Owner] = {}
        self.exti: dict[int, str] = {}
        self.watchdog: dict[str, Any] | None = None
        self.groups: dict[str, FakeGroup] = {cls.prefix: cls(self) for cls in discover_groups()}
        super().__post_init__()

    @property
    def spec(self) -> _Family:
        return _FAMILIES[self.family]

    @property
    def kernel_clock(self) -> int:
        assert self.sysclk is not None
        return self.sysclk

    def boot(self) -> None:
        self.opened.clear()
        self.claims = {}
        self.timer_owners = {}
        self.resources = {}
        self.exti = {}
        self.watchdog = None
        for group in self.groups.values():
            group.boot()
        super().boot()

    def event(self, line: str) -> None:
        """Print an asynchronous `EVT` line now, as a complete line whatever the output style."""
        self._write(f"\r\n{line}\r\n")

    def boot_message(self) -> str:
        return f"EVT boot board={self.board} family={self.family} sysclk={self.sysclk} reset={self.reset_cause}"

    def group(self, prefix: str) -> FakeGroup | None:
        return self.groups.get(prefix)

    def lookup(self, name: str) -> Handler | None:
        handler = self.handlers.get(name)
        if handler is not None:
            return handler
        if name in self.spec.unsupported:
            return lambda device, args, options: "ERR unsupported"
        prefix, _, verb = name.partition(".")
        group = self.groups.get(prefix)
        method = group.handler(verb) if group is not None and verb else None
        if method is None:
            method = getattr(self, "_cmd_" + name.replace(".", "_"), None)
        if method is None:
            return None
        command = method

        def run(device: FakeTerminalDevice, args: list[str], options: dict[str, str]) -> str | list[str] | None:
            try:
                return command(args, options)
            except FakeError as error:
                return f"ERR {error.reason}"

        return run

    # pins and instances

    def pin(self, text: str | None) -> str | None:
        """`HilPinNamingDefault::Parse`: canonical name of an alias or of `P<port><index>` with a port of the MCU
        and an index up to 15, else `ERR pin`. Bonding is checked where the pin is used, as the firmware does."""
        if text is None:
            return None
        resolved = (self.pins or {}).get(text, text)
        match = _PIN_RE.match(resolved)
        if match is None:
            _fail("pin")
        assert match is not None
        port, index = match.group(1).upper(), int(match.group(2))
        if port not in self.spec.ports or index > 15:
            _fail("pin")
        return f"P{port}{index}"

    def bonded(self, pin: str) -> bool:
        """`IsBonded`: the package bonds the pin out."""
        return bool(self.spec.bonded.get(pin[1], 0) & (1 << int(pin[2:])))

    def reserved(self) -> set[str]:
        terminal = {pin for alias, pin in (self.pins or {}).items() if alias in ("terminaltx", "terminalrx")}
        return terminal | {self.spec.debug_led, *self.spec.reserved}

    def supports(self, function: str, instance: int, pin: str | None) -> bool:
        """`SupportsFunction`: a bonded pin the pinout table offers for `function` of `instance`."""
        return pin is not None and self.bonded(pin) and pin in self.spec.functions.get(function, {}).get(instance, ())

    def supports_analog(self, pin: str) -> bool:
        return self.bonded(pin) and pin in self.spec.analog

    def check_function(self, function: str, instance: int, pin: str | None) -> None:
        """`ERR pin` unless a given pin offers `function` of `instance`."""
        if pin is not None and not self.supports(function, instance, pin):
            _fail("pin")

    def first_function_pin(self, function: str, instance: int) -> str | None:
        """`FindFunctionPin`: the first bonded pin of the table that is not reserved."""
        for pin in self.spec.functions.get(function, {}).get(instance, ()):
            if self.bonded(pin) and pin not in self.reserved():
                return pin
        return None

    def check_pins(self, owner: Owner, pins: Iterable[str | None], analog: bool = False) -> None:
        """`HilPinPool::Claim`: `pin` for a pin the package lacks, `busy` for a reserved or held pin (analog users
        share a pin)."""
        seen: set[str] = set()
        for pin in pins:
            if pin is None:
                continue
            if not self.bonded(pin):
                _fail("pin")
            if pin in self.reserved():
                _fail("busy")
            holder = self.claims.get(pin)
            if holder is not None and holder[0] != owner and not (analog and holder[1]):
                _fail("busy")
            if pin in seen and not analog:
                _fail("busy")
            seen.add(pin)

    def claim(self, owner: Owner, pins: Iterable[str | None], analog: bool = False) -> None:
        for pin in pins:
            if pin is not None:
                self.claims.setdefault(pin, (owner, analog))

    def release(self, owner: Owner) -> None:
        """Everything `owner` holds: pins, timers and resources."""
        self.claims = {pin: holder for pin, holder in self.claims.items() if holder[0] != owner}
        self.timer_owners = {timer: holder for timer, holder in self.timer_owners.items() if holder != owner}
        self.release_resources(owner)

    def check_resources(self, owner: Owner, resources: Iterable[tuple[str, int]]) -> None:
        """`ResourceAllocation::Claim` without claiming: `busy` when another owner holds one of them."""
        for kind, index in resources:
            if kind not in RESOURCES or not 0 <= index < _RESOURCE_INDEX_LIMIT:
                raise ValueError(f"no resource {kind} {index}")
            if self.resources.get((kind, index), owner) != owner:
                _fail("busy")

    def claim_resource(self, kind: str, index: int, owner: Owner) -> None:
        """`ResourceAllocation::Claim`: `busy` when another owner holds it; the same owner may claim it again."""
        self.check_resources(owner, [(kind, index)])
        self.resources[(kind, index)] = owner

    def release_resources(self, owner: Owner) -> None:
        self.resources = {key: holder for key, holder in self.resources.items() if holder != owner}

    def timer_exists(self, timer: int) -> bool:
        return timer in self.spec.timers

    def open_instance(
        self,
        group: str,
        key: str,
        state: dict[str, Any],
        pins: Iterable[str | None],
        analog: bool = False,
        timer: int | None = None,
        resources: Iterable[tuple[str, int]] = (),
    ) -> None:
        """Claims an instance after its arguments were validated: everything here answers `ERR busy`."""
        owner = (group, key)
        pins = list(pins)
        resources = list(resources)
        held = sum(1 for opened_group, _ in self.opened if opened_group == group)
        if owner in self.opened or held >= OPEN_LIMITS.get(group, 1):
            _fail("busy")
        if timer is not None and self.timer_owners.get(timer, owner) != owner:
            _fail("busy")
        self.check_resources(owner, resources)
        self.check_pins(owner, pins, analog)
        self.claim(owner, pins, analog)
        if timer is not None:
            self.timer_owners[timer] = owner
        for kind, index in resources:
            self.resources[(kind, index)] = owner
        self.opened[owner] = state

    def find_instance(self, group: str, key: str) -> dict[str, Any]:
        """The state of an open instance, else `ERR notopen`."""
        state = self.opened.get((group, key))
        if state is None:
            _fail("notopen")
        assert state is not None
        return state

    def close_instance(self, group: str, key: str) -> str:
        del self.opened[(group, key)]
        self.release((group, key))
        return "OK"

    def _index(self, group: str, text: str) -> int:
        """`HilSingleInstance::Parse`: the instance number of the command."""
        return _number(text, 0, _INSTANCES[group] - 1)

    def _find(self, group: str, text: str) -> tuple[str, dict[str, Any]]:
        key = str(self._index(group, text))
        return key, self.find_instance(group, key)

    # general

    def _cmd_ping(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        return "OK"

    def _cmd_info(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        return f"OK board={self.board} family={self.family} sysclk={self.sysclk} reset={self.reset_cause} uid={self.uid}"

    def _cmd_board_pins(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        return "OK " + ",".join(f"{alias}={pin}" for alias, pin in (self.pins or {}).items())

    def _cmd_delay(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        self.sleep(_number(args[0], 0, _DELAY_MAX_MS) / 1000)
        return "OK"

    def _cmd_reset(self, args: list[str], options: dict[str, str]) -> None:
        self.reset_cause = "sw"
        self.boot()
        return None

    # gpio (`HilGpioCommands`)

    def _gpio(self, text: str) -> tuple[str, dict[str, Any]]:
        """`HilGpioCommands::Find`: `pin` for a name that is no pin, `notopen` for a pin it does not hold."""
        pin = self.pin(text)
        assert pin is not None
        state = self.opened.get(("gpio", pin))
        if state is None:
            _fail("notopen")
        assert state is not None
        return pin, state

    def _gpio_free(self, pin: str) -> None:
        line = int(pin[2:])
        if self.exti.get(line) == pin:
            del self.exti[line]
        del self.opened[("gpio", pin)]
        self.release(("gpio", pin))

    def _cmd_gpio_cfg(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, ("pull", "drive"))
        pin = self.pin(args[0])
        assert pin is not None
        mode = args[1]
        if mode not in ("in", "out", "od"):
            _fail("usage")
        pull = _choice(options, "pull", ("none", "up", "down"), "none")
        drive = _choice(options, "drive", ("low", "medium", "fast", "high"), "low")
        if mode == "od" and pull != "none":
            _fail("usage")
        if ("gpio", pin) in self.opened:
            self._gpio_free(pin)
        self.open_instance("gpio", pin, {"mode": mode, "pull": pull, "drive": drive, "irq": "off"}, [pin])
        if mode == "out":
            self.gpio_levels[pin] = 0
        elif mode == "od":
            self.gpio_levels[pin] = 1
        else:
            self.gpio_levels.setdefault(pin, 1 if pull == "up" else 0)
        self.gpio_counts[pin] = 0
        return "OK"

    def _cmd_gpio_set(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        pin, _ = self._gpio(args[0])
        if args[1] not in ("0", "1"):
            _fail("usage")
        self.gpio_levels[pin] = int(args[1])
        return "OK"

    def _cmd_gpio_get(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        pin, _ = self._gpio(args[0])
        return f"OK value={self.gpio_levels.get(pin, 0)}"

    def _cmd_gpio_pulse(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 3, 3)
        pin, state = self._gpio(args[0])
        count = _number(args[1], 1, _PULSES_MAX)
        _number(args[2], 1, _PULSE_PERIOD_MAX_MS)
        if state["mode"] == "in":
            _fail("usage")
        self.gpio_levels[pin] = self.gpio_levels.get(pin, 0) ^ (count & 1)
        return "OK"

    def supports_interrupt(self, pin: str) -> bool:
        """An EXTI line serves one port at a time."""
        owner = self.exti.get(int(pin[2:]))
        return self.bonded(pin) and (owner is None or owner == pin)

    def _cmd_gpio_irq(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, ("type",))
        pin, state = self._gpio(args[0])
        edge = args[1]
        if edge not in ("rising", "falling", "both", "off"):
            _fail("usage")
        _choice(options, "type", ("immediate", "dispatched"), "dispatched")
        if not self.supports_interrupt(pin):
            _fail("unsupported")
        line = int(pin[2:])
        if edge == "off":
            self.exti.pop(line, None)
        else:
            self.exti[line] = pin
        state["irq"] = edge
        return "OK"

    def _cmd_gpio_count(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("clear",))
        pin, _ = self._gpio(args[0])
        clear = _flag(options, "clear")
        count = self.gpio_counts.get(pin, 0)
        if clear:
            self.gpio_counts[pin] = 0
        return f"OK count={count}"

    def _cmd_gpio_release(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        pin, _ = self._gpio(args[0])
        self._gpio_free(pin)
        return "OK"

    # pwm (`PwmFactory.cpp` `Evaluate`)

    def _pwm_entry(self, text: str) -> _PwmOutput:
        """`ParseOutput`: `<pin>[:<npin>]`, `-` for an unused position."""
        first, separator, second = text.partition(":")
        if not separator:
            second = "-"
        elif ":" in second:
            _fail("usage")
        pin = None if first == "-" else self.pin(first)
        npin = None if second == "-" else self.pin(second)
        if pin is None and npin is None:
            _fail("usage")
        return _PwmOutput(0, pin, npin)

    def _pwm_outputs(self, options: Mapping[str, str]) -> list[_PwmOutput]:
        """`ParseOutputs`: the `channels`/`pins` lists (`usage`, `range`, `pin` for names that are no pin)."""
        channels_text, pins_text = options.get("channels"), options.get("pins")
        if channels_text is None and pins_text is None:
            _fail("usage")
        channels = None if channels_text is None else [_number(token, 1, _PWM_CHANNELS_MAX) for token in _tokens(channels_text)]
        entries = None if pins_text is None else [self._pwm_entry(token) for token in _tokens(pins_text)]
        count = len(channels) if channels is not None else len(entries or [])
        if not 1 <= count <= _PWM_CHANNELS_MAX or (channels is not None and entries is not None and len(entries) != len(channels)):
            _fail("usage")
        if channels is not None and len(set(channels)) != len(channels):
            _fail("usage")
        outputs = entries if entries is not None else [_PwmOutput(0) for _ in range(count)]
        for position, output in enumerate(outputs):
            if channels is not None:
                output.channel = channels[position]
        return outputs

    def _pwm_channel_of(self, timer: int, output: _PwmOutput) -> int | None:
        """`ChannelOfPin`: the first channel (or complementary channel) the pin offers on `timer`."""
        if output.pin is not None:
            return next((channel for channel in range(1, 5) if self.supports(f"timerChannel{channel}", timer, output.pin)), None)
        return next((channel for channel in range(1, 4) if self.supports(f"timerChannel{channel}N", timer, output.npin)), None)

    def _pwm_resolve(self, timer: int, outputs: list[_PwmOutput]) -> None:
        """`ResolveOutputs`: channels from pins, the timer's channels and the pins of each channel."""
        for output in outputs:
            if output.channel == 0:
                channel = self._pwm_channel_of(timer, output)
                if channel is None:
                    _fail("pin")
                assert channel is not None
                output.channel = channel
        if len({output.channel for output in outputs}) != len(outputs):
            _fail("usage")
        for output in outputs:
            channel = output.channel
            if not expect.timer_has_channel(timer, channel) or (
                output.npin is not None and not expect.timer_has_complementary(timer, channel)
            ):
                _fail("unsupported")
            if output.pin is None and output.npin is None:
                output.pin = self.first_function_pin(f"timerChannel{channel}", timer)
                if output.pin is None:
                    _fail("pin")
            self.check_function(f"timerChannel{channel}", timer, output.pin)
            self.check_function(f"timerChannel{channel}N", timer, output.npin)

    def _cmd_pwm_open(self, args: list[str], options: dict[str, str]) -> str:
        keys = ("channels", "pins", "freq", "mode", "prescaler", "dead", "inv", "invn", "idle", "idlen", "brk", "brkpol", "brkauto", "sync")
        _shape(args, options, 1, 1, (*keys, "preload", "brkfilter", "trgo"))
        timer = self._index("pwm", args[0])
        if not self.timer_exists(timer):
            _fail("range")
        freq = _number(options.get("freq", "10000"), 1)
        mode = _choice(options, "mode", _PWM_MODES, "edge")
        trgo = _choice(options, "trgo", _PWM_TRIGGER_OUTPUTS, "reset") if "trgo" in options else None
        prescaler = _number(options.get("prescaler", "0"), 0, expect.PWM_PRESCALER_MAX)
        dead = None if options.get("dead", "off") == "off" else _number(options["dead"], 0, expect.PWM_DEAD_MAX_NS)
        flags = {key: _flag(options, key) for key in ("inv", "invn", "idle", "idlen", "brkauto", "sync")}
        preload = _flag(options, "preload", True)
        _choice(options, "brkpol", ("low", "high"), "high")
        brkfilter = _number(options["brkfilter"], 0, _PWM_BREAK_FILTER_MAX) if "brkfilter" in options else None
        brk = self.pin(options.get("brk"))
        if brkfilter is not None and brk is None:
            _fail("usage")
        outputs = self._pwm_outputs(options)
        # PwmStm asserts a counter mode select instance for every alignment but edgeAligned (PwmStm.cpp:224).
        if mode != "edge" and not expect.timer_has_center_mode(timer):
            _fail("unsupported")
        # `IS_TIM_MASTER_INSTANCE`: the timers with a counter mode select, on both MCUs.
        if trgo is not None and not expect.timer_has_center_mode(timer):
            _fail("unsupported")
        complementary = any(output.npin is not None for output in outputs)
        needs_break = complementary or dead is not None or flags["idle"] or flags["idlen"] or brk is not None
        if needs_break and not expect.timer_has_break(timer):
            _fail("unsupported")
        if brkfilter and not expect.timer_has_break_filter(self.family, timer):
            _fail("unsupported")
        self._pwm_resolve(timer, outputs)
        self.check_function("timerBreak", timer, brk)
        pwmclk = expect.pwm_clock(self.kernel_clock, prescaler)
        counter_max = expect.timer_counter_max(timer)
        alignment = _pwm_alignment(mode)
        if not expect.pwm_fits(pwmclk, freq, alignment, counter_max):
            _fail("range")
        channels = [(output.channel, output.pin, output.npin) for output in outputs]
        pins = [pin for _, first, second in channels for pin in (first, second)] + [brk]
        state = {"channels": channels, "freq": freq, "mode": mode, "pwmclk": pwmclk, "counter_max": counter_max, "running": False}
        state.update(alignment=alignment, dead=dead, brk=brk, brkfilter=brkfilter, sync=flags["sync"], duties=None)
        state.update(preload=preload, trgo=trgo)
        self.open_instance("pwm", str(timer), state, pins, timer=timer)
        return f"OK pwmclk={pwmclk}"

    def _cmd_pwm_duty(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 1 + _PWM_CHANNELS_MAX)
        _, state = self._find("pwm", args[0])
        duties = args[1:]
        if len(duties) not in (1, len(state["channels"])):
            _fail("usage")
        state["duties"] = [_duty(duty) for duty in duties]
        state["running"] = True
        return "OK"

    def _cmd_pwm_freq(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        _, state = self._find("pwm", args[0])
        freq = _number(args[1], 1)
        if not expect.pwm_fits(state["pwmclk"], freq, state["alignment"], state["counter_max"]):
            _fail("range")
        state["freq"] = freq
        return "OK"

    def _cmd_pwm_stop(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        _, state = self._find("pwm", args[0])
        state["running"] = False
        return "OK"

    def _cmd_pwm_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key, _ = self._find("pwm", args[0])
        return self.close_instance("pwm", key)

    # uart (`UartFactory.cpp` `Evaluate`)

    def _cmd_uart_open(self, args: list[str], options: dict[str, str]) -> str:
        keys = ("lp", "tx", "rx", "rts", "cts", "baud", "parity", "flow", "swap", "dma", "duplex", "sync", "sendonly")
        _shape(args, options, 1, 1, keys)
        index = self._index("uart", args[0])
        lp = _flag(options, "lp")
        tx, rx, rts, cts = (self.pin(options.get(key)) for key in ("tx", "rx", "rts", "cts"))
        baud = _number(options.get("baud", "115200"), expect.UART_BAUD_MIN, expect.UART_BAUD_MAX)
        parity = _choice(options, "parity", ("none", "even", "odd"), "none")
        flow = _choice(options, "flow", ("none", "rts", "cts", "rtscts"), "none")
        swap, dma, duplex, sync, sendonly = (_flag(options, key) for key in ("swap", "dma", "duplex", "sync", "sendonly"))
        if index not in (self.spec.lpuarts if lp else self.spec.usarts):
            _fail("range")
        if dma + duplex + sync + sendonly > 1:
            _fail("usage")
        if (flow in ("rts", "rtscts")) != (rts is not None) or (flow in ("cts", "rtscts")) != (cts is not None):
            _fail("usage")
        synchronous = sync or sendonly
        if (lp and (duplex or sync)) or (synchronous and (parity != "none" or swap)):
            _fail("unsupported")
        # `SynchronousUartStmSendOnly` takes an RTS pin but no CTS pin, and LPUARTs on STM32WB only.
        if sendonly and (flow not in ("none", "rts") or (lp and not self.spec.lpuart_send_only)):
            _fail("unsupported")
        if not synchronous and flow not in ("none", "rtscts"):
            _fail("unsupported")
        if (dma or duplex) and (lp, index) not in self.spec.uart_dma:
            _fail("unsupported")
        if not expect.uart_baud_fits(self.kernel_clock, baud, lp, expect.uart_baud_max(self.family)):
            _fail("range")
        terminal = not lp and index == _TERMINAL_USART
        defaults = self._terminal_pins() if terminal else self.spec.default_lpuart if lp and index == _DEFAULT_LPUART else None
        if defaults is not None and tx is None and rx is None and rts is None and cts is None:
            tx, rx = defaults
        if tx is None or (rx is None and not sendonly):
            _fail("usage")
        prefix = "lpuart" if lp else "uart"
        for function, pin in (("Tx", tx), ("Rx", rx), ("Rts", rts), ("Cts", cts)):
            self.check_function(prefix + function, index, pin)
        if terminal:
            _fail("busy")
        state = {"lp": lp, "baud": baud, "parity": parity, "flow": flow, "swap": swap, "dma": dma, "duplex": duplex, "sync": sync}
        state["sendonly"] = sendonly
        self.open_instance("uart", str(index), state, [tx, rx, rts, cts])
        self.uart_rx[index] = bytearray()
        return "OK"

    def _terminal_pins(self) -> tuple[str, str]:
        """The terminal USART1 defaults to its own pins, so a bare `uart.open 1` passes the argument checks."""
        pins = {**self.spec.pins, **(self.pins or {})}
        return pins["terminaltx"], pins["terminalrx"]

    def _cmd_uart_send(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        self._find("uart", args[0])
        if not _hex(args[1], _UART_TRANSMIT_CAPACITY):
            _fail("usage")
        return "OK"

    def _cmd_uart_recv(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("timeout", "len"))
        key, state = self._find("uart", args[0])
        _number(options.get("timeout", "1000"), 0, _RECEIVE_TIMEOUT_MAX_MS)
        if "len" in options:
            _number(options["len"], 1, _UART_RECEIVE_CAPACITY)
        if state["sendonly"]:
            return "OK data=-"
        buffer = self.uart_rx.setdefault(int(key), bytearray())
        data = bytes(buffer[:_UART_RECEIVE_CAPACITY])
        del buffer[: len(data)]
        return f"OK data={format_hex(data) or '-'}"

    def _cmd_uart_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key, _ = self._find("uart", args[0])
        return self.close_instance("uart", key)

    # spi (`SpiFactory.cpp` `Evaluate`)

    def _cmd_spi_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("clk", "mosi", "miso", "cs", "baud", "mode", "dma", "sync", "bits", "lsb", "nss"))
        index = self._index("spi", args[0])
        if index not in self.spec.spis:
            _fail("range")
        clk, mosi, miso, cs, nss = (self.pin(options.get(key)) for key in ("clk", "mosi", "miso", "cs", "nss"))
        baud = _number(options.get("baud", "1000000"), 1)
        mode = _number(options.get("mode", "0"), 0, 3)
        bits = _number(options.get("bits", "8"), *_SPI_BITS)
        dma, sync, lsb = _flag(options, "dma"), _flag(options, "sync"), _flag(options, "lsb")
        if clk is None or mosi is None or miso is None:
            _fail("usage")
        if (dma and sync) or (nss is not None and cs is not None):
            _fail("usage")
        if not expect.spi_baud_fits(self.kernel_clock, baud):
            _fail("range")
        for function, pin in (("spiClock", clk), ("spiMosi", mosi), ("spiMiso", miso), ("spiSlaveSelect", nss)):
            self.check_function(function, index, pin)
        if cs is not None and not self.bonded(cs):
            _fail("pin")
        # Only `SpiMasterStmDma` has a data size configurator; a limited instance takes 8- and 16-bit frames only.
        if (bits != 8 and not dma) or (index in self.spec.spi_limited and bits not in _SPI_LIMITED_BITS):
            _fail("unsupported")
        state = {"baud": baud, "clock": expect.spi_clock(self.kernel_clock, baud), "mode": mode, "dma": dma, "sync": sync, "cs": cs}
        state.update(bits=bits, lsb=lsb, nss=nss)
        self.open_instance("spi", str(index), state, [clk, mosi, miso, cs, nss], resources=[("spi", index)])
        return "OK"

    def _cmd_spi_xfer(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, ("rx", "continue"))
        self._find("spi", args[0])
        data = _hex(args[1], _SPI_CAPACITY)
        size = _number(options.get("rx", str(len(data))), 0, _SPI_CAPACITY)
        _flag(options, "continue")
        if max(len(data), size) == 0:
            _fail("usage")
        return f"OK rx={format_hex(bytes([self.spi_miso]) * size) or '-'}"

    def _cmd_spi_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key, _ = self._find("spi", args[0])
        return self.close_instance("spi", key)

    # adc (`AdcFactory.cpp` `ParseKey` and `Parse`)

    def _adc_key(self, args: list[str]) -> str:
        adc = _number(args[0], 0, _ADC_KEY_MAX)
        if adc != self.spec.adc:
            _fail("range")
        return str(adc)

    def _adc_find(self, args: list[str]) -> tuple[str, dict[str, Any]]:
        key = self._adc_key(args)
        state = self.opened.get(("adc", key))
        if state is None:
            _fail("notopen")
        assert state is not None
        return key, state

    def _cmd_adc_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("pins", "sampling", "timer", "rate", "trgo"))
        key = self._adc_key(args)
        _choice(options, "sampling", self.spec.sampling, expect.ADC_DEFAULT_SAMPLING[self.family])
        timer = _number(options["timer"], 0, _TIMER_TABLE_SIZE) if "timer" in options else None
        rate = _number(options.get("rate", str(expect.ADC_DEFAULT_RATE)), 1, expect.ADC_RATE_MAX)
        trgo = _number(options["trgo"], 0, _TIMER_TABLE_SIZE) if "trgo" in options else None
        if "rate" in options and timer is None:
            _fail("usage")
        if trgo is not None and (timer is not None or "rate" in options):
            _fail("usage")
        if "pins" not in options:
            _fail("usage")
        entries = options["pins"].split(",")
        if len(entries) > expect.ADC_MAX_PINS:
            _fail("range")
        pins: list[str] = []
        for entry in entries:
            pin = self.pin(entry)
            assert pin is not None
            if not self.supports_analog(pin):
                _fail("pin")
            pins.append(pin)
        if timer is not None and not self.timer_exists(timer):
            _fail("range")
        if timer is not None and timer not in expect.ADC_TRIGGER_TIMERS:
            _fail("unsupported")
        if trgo is not None:
            if not self.timer_exists(trgo):
                _fail("range")
            # The sequence runs on the TRGO of a timer the pwm group drives; the adc group does not own it.
            holder = self.timer_owners.get(trgo)
            if trgo not in _ADC_PWM_TRIGGER_TIMERS[self.family] or holder is None:
                _fail("unsupported")
            assert holder is not None
            if holder[0] != "pwm":
                _fail("busy")
        state = {"pins": pins, "timer": timer, "rate": rate, "trgo": trgo}
        resources = [("adc", self.spec.adc), _ADC_DMA]
        self.open_instance("adc", key, state, pins, analog=True, timer=timer, resources=resources)
        return "OK"

    def _cmd_adc_measure(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("n",))
        _, state = self._adc_find(args)
        runs = _number(options.get("n", "1"), 1, expect.ADC_MAX_VALUES)
        if runs * len(state["pins"]) > expect.ADC_MAX_VALUES:
            _fail("range")
        rate = state["rate"] if state["trgo"] is None else self._trigger_rate(state["trgo"])
        if state["timer"] is not None or state["trgo"] is not None:
            duration = math.inf if rate is None else expect.adc_measure_time(runs, rate)
            if duration > _ADC_MEASURE_TIMEOUT:
                self.sleep(_ADC_MEASURE_TIMEOUT)
                return "ERR timeout"
            self.sleep(duration)
        samples = [self.adc_codes.get(pin, 2048) for _ in range(runs) for pin in state["pins"]]
        reply = "OK samples=" + ",".join(str(sample) for sample in samples)
        # Tests time this reply, which is long enough for its transmission to count.
        self.sleep(expect.terminal_line_time(reply, self.terminal_baud))
        return reply

    def _trigger_rate(self, timer: int) -> int | None:
        """Runs per second of `adc.open trgo=`: one per period of the pwm group's timer, none while it is stopped."""
        state = self.opened.get(("pwm", str(timer)))
        return state["freq"] if state is not None and state["running"] else None

    def _cmd_adc_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key, _ = self._adc_find(args)
        return self.close_instance("adc", key)

    # qei (`QeiFactory.cpp` `Evaluate`)

    def _cmd_qei_open(self, args: list[str], options: dict[str, str]) -> str:
        keys = ("lp", "a", "b", "idx", "res", "offset", "inva", "invb", "cap", "filter", "vel")
        _shape(args, options, 1, 1, keys)
        timer = self._index("qei", args[0])
        lp = _flag(options, "lp")
        if lp and not self.spec.lptims:
            _fail("unsupported")
        if lp and timer not in self.spec.lptim_encoders:
            _fail("range")
        if not lp and not self.timer_exists(timer):
            _fail("range")
        if not lp and timer not in expect.ENCODER_TIMERS:
            _fail("unsupported")
        # The LPTIM encoder counts both inputs (x4) or one edge of both (x2); it has no offset and one polarity.
        if lp and (any(key in options for key in ("offset", "invb")) or options.get("cap") in ("a", "b")):
            _fail("unsupported")
        maximum = UINT32_MAX if not lp and timer in expect.TIMERS_32BIT else _QEI_RESOLUTION_16BIT
        res = _number(options.get("res", "4096"), 2, maximum)
        offset = _number(options.get("offset", "0"))
        if offset >= res:
            _fail("range")
        _flag(options, "inva")
        _flag(options, "invb")
        cap = _choice(options, "cap", _QEI_LP_CAPTURES if lp else _QEI_CAPTURES, "ab")
        filter_samples = _number(options.get("filter", "0"), 0, 15)
        if options.get("vel") != "off":
            _number(options.get("vel", "1000"), 1, _QEI_VELOCITY_MAX_US)
        if lp and filter_samples not in _LPTIM_FILTERS:
            _fail("range")
        a, b, idx = (self.pin(options.get(key)) for key in ("a", "b", "idx"))
        default = self.spec.default_qei
        if a is None and b is None and idx is None and not lp and timer == default[0]:
            a, b, idx = default[1:]
        if a is None or b is None:
            _fail("usage")
        inputs = ("lpTimerInput1", "lpTimerInput2") if lp else ("timerChannel1", "timerChannel2")
        if not self.supports(inputs[0], timer, a) or not self.supports(inputs[1], timer, b):
            _fail("pin")
        if idx is not None and not self.bonded(idx):
            _fail("pin")
        state = {"lp": lp, "res": res, "pos": offset, "dir": "fwd", "speed": 0, "idx": idx, "cap": cap}
        resources = [("lpTimer", timer)] if lp else []
        self.open_instance("qei", str(timer), state, [a, b, idx], timer=None if lp else timer, resources=resources)
        return "OK"

    def _cmd_qei_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        _, state = self._find("qei", args[0])
        return f"OK pos={state['pos']} dir={state['dir']} speed={state['speed']} res={state['res']}"

    def _cmd_qei_index(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        _, state = self._find("qei", args[0])
        if state["idx"] is None:
            _fail("unsupported")
        return f"OK idx={self.gpio_levels.get(state['idx'], 0)}"

    def _cmd_qei_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key, _ = self._find("qei", args[0])
        return self.close_instance("qei", key)

    # watchdog (`HilWatchDogCommands::Parse` around `WatchDogFactory.cpp` `Prepare`)

    @property
    def pclk1(self) -> int:
        return self.kernel_clock

    def _cmd_wdt_start(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("timeout", "feed", "pin"))
        index = self._index("wdt", args[0])
        timeout = _number(options["timeout"], 1, expect.WDT_TIMEOUT_MAX_MS) if "timeout" in options else None
        pin = self.pin(options.get("pin"))
        prescaler = None if timeout is None else expect.wwdg_prescaler_for(timeout, self.pclk1)
        if timeout is not None and prescaler is None:
            _fail("range")
        if pin is not None and not self.bonded(pin):
            _fail("pin")
        feed = _choice(options, "feed", ("auto", "manual"), "auto")
        if timeout is None:
            _fail("usage")
        assert prescaler is not None
        if self.watchdog is not None:
            _fail("busy")
        owner = ("wdt", str(index))
        self.check_pins(owner, [pin])
        self.claim(owner, [pin])
        if pin is not None:
            self.gpio_levels[pin] = 0
        period = expect.wwdg_period(self.pclk1, prescaler)
        self.watchdog = {
            "index": index,
            "feed": feed,
            "pin": pin,
            "period": period,
            "tick": expect.wwdg_tick(self.pclk1, prescaler),
            "next": self.clock() + period,
            "reset_at": None,
            "warnings": 0,
            "sending": None,
        }
        return "OK"

    def _cmd_wdt_feed(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        self._index("wdt", args[0])
        watchdog = self.watchdog
        if watchdog is None:
            _fail("notopen")
        assert watchdog is not None
        watchdog["next"] = self.clock() + watchdog["period"]
        watchdog["reset_at"] = None
        if watchdog["sending"] is not None:
            self.event(watchdog["sending"])
            watchdog["sending"] = None
        return "OK"

    def poll(self) -> None:
        """Time-driven output of the groups, then of the watchdog."""
        for group in self.groups.values():
            group.poll()
        self._poll_watchdog()

    def _poll_watchdog(self) -> None:
        """Early warnings every period; `feed=auto` refreshes on each, otherwise the WWDG resets the board one
        counter tick after a warning that was not answered with `wdt.feed`. A tick shorter than the transmission
        of the `EVT wdt` line cuts the line: the host gets the part sent before the reset."""
        watchdog = self.watchdog
        if watchdog is None:
            return
        now = self.clock()
        for _ in range(1000):
            if watchdog["reset_at"] is not None and now >= watchdog["reset_at"]:
                if watchdog["sending"] is not None:
                    sent = int(watchdog["tick"] * self.terminal_baud / expect.uart_frame_bits("none"))
                    # The line break that starts the boot banner ends the fragment.
                    self._write(f"\r\n{watchdog['sending']}"[:sent] + "\r\n")
                self.reset_cause = "wwdg"
                self.boot()
                return
            if watchdog["next"] > now:
                return
            watchdog["warnings"] += 1
            if watchdog["pin"] is not None:
                self.gpio_levels[watchdog["pin"]] = 1 - self.gpio_levels.get(watchdog["pin"], 0)
            line = expect.wwdg_warning_line(watchdog["index"], watchdog["warnings"])
            if watchdog["feed"] == "auto":
                self.event(line)
                watchdog["next"] += watchdog["period"]
                continue
            watchdog["reset_at"] = watchdog["next"] + watchdog["tick"]
            watchdog["next"] = math.inf
            if expect.terminal_line_time(line, self.terminal_baud) <= watchdog["tick"]:
                self.event(line)
            else:
                watchdog["sending"] = line
