import pytest
from ad3_waveforms_bench.protocol import ProtocolError, parse_response

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS
from hal_st_validation.protocol import ERROR_REASONS, PIN_ALIASES, is_alias, is_pin, normalize_pin, parse_pin_map, pin_parts


@pytest.mark.parametrize("pin", ["PA0", "pa15", "PB9", "PC10", "PH3", "PK15", "pe4"])
def test_valid_pins(pin):
    assert is_pin(pin)


@pytest.mark.parametrize("pin", ["PA16", "PA05", "PL0", "PZ1", "P1", "PA", "A1", "PA1x", "PA-1", "led0", ""])
def test_invalid_pins(pin):
    assert not is_pin(pin)


def test_normalize_pins():
    assert normalize_pin("pa15") == "PA15"
    assert normalize_pin("Pc0") == "PC0"
    assert normalize_pin("PK15") == "PK15"
    assert normalize_pin("tim1ch1") == "tim1ch1", "a generic alias passes through unresolved"
    assert normalize_pin("TIM1CH1", {"tim1ch1": "PA8"}) == "PA8"
    assert normalize_pin("qei2idx", {"tim1ch1": "PA8"}) == "qei2idx"
    assert normalize_pin("gpio0", {"gpio0": "pc6"}) == "PC6", "table entries are normalised too"
    for bad in ("PA16", "PL0", "PA05", "nosuchalias", "phasea"):
        with pytest.raises(ProtocolError):
            normalize_pin(bad)
    with pytest.raises(ProtocolError):
        normalize_pin("qei2idx", {"tim1ch1": "PA8"}, strict=True)


def test_pin_parts():
    assert pin_parts("PA15") == ("A", 15)
    assert pin_parts("ph3") == ("H", 3)
    with pytest.raises(ProtocolError):
        pin_parts("led0")


@pytest.mark.parametrize(
    "alias",
    [
        "terminaltx",
        "terminalrx",
        "ain0",
        "ain19",
        "tim1ch1",
        "tim17ch4",
        "tim1ch3n",
        "tim17ch1n",
        "tim16bkin",
        "qei1a",
        "qei17idx",
        "lptim1in1",
        "lptim2in2",
        "spi3cs",
        "spi1miso",
        "usart2rts",
        "usart3cts",
        "lpuart1tx",
        "lpuart1cts",
        "led7",
        "gpio15",
        "sw1",
        "sw3",
    ],
)
def test_generic_aliases(alias):
    assert is_alias(alias)
    assert is_alias(alias.upper())


@pytest.mark.parametrize(
    "name",
    [
        "ain20",
        "tim0ch1",
        "tim18ch1",
        "tim1ch5",
        "tim1ch4n",
        "tim1ch0",
        "qei0a",
        "qei1c",
        "lptim3in1",
        "lptim1in3",
        "spi0clk",
        "spi4clk",
        "spi1sck",
        "usart4tx",
        "uart1tx",
        "lpuart2tx",
        "led8",
        "gpio16",
        "sw0",
        "sw4",
        "m0pwm0",
        "can0rx",
        "phasea",
        "button1",
    ],
)
def test_not_aliases(name):
    assert not is_alias(name)


def test_alias_scheme_size():
    """terminal 2, ain 20, timers 17 * (4 + 3 + 1), qei 17 * 3, lptim 4, spi 12, usart 12, lpuart 4, led 8, gpio 16, sw 3."""
    assert len(PIN_ALIASES) == 2 + 20 + 17 * 8 + 17 * 3 + 4 + 12 + 12 + 4 + 8 + 16 + 3


@pytest.mark.parametrize("table", [WB55_PINS, WBA55_PINS])
def test_board_tables_use_the_scheme(table):
    assert all(is_alias(alias) for alias in table)
    assert all(is_pin(pin) and normalize_pin(pin) == pin for pin in table.values())


def test_error_reasons():
    assert {"usage", "pin", "busy", "notopen", "unsupported", "range", "timeout", "failed"} == ERROR_REASONS


def test_pin_map():
    response = parse_response("OK terminaltx=PB6,terminalrx=PB7,ain4=PC3,led0=PB0")
    assert parse_pin_map(response) == {"terminaltx": "PB6", "terminalrx": "PB7", "ain4": "PC3", "led0": "PB0"}
    spaced = parse_response("OK ain2=PA7 ain3=pa6")
    assert parse_pin_map(spaced) == {"ain2": "PA7", "ain3": "PA6"}
    assert parse_pin_map(parse_response("OK")) == {}
