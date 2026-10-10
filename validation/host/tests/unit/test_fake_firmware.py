import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation import expect
from hal_st_validation.config import load_board
from hal_st_validation.fake_firmware import UNSUPPORTED_COMMANDS, WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial

BOARDS = {"stm32wb55": "nucleo_wb55rg", "stm32wba55": "nucleo_wba55cg"}


class Clock:
    def __init__(self) -> None:
        self.now = 100.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def make_terminal(style="line", chunk=0, noise=False, **kwargs):
    firmware = FakeFirmware(style=style, noise=noise, **kwargs)
    serial = FakeSerial(firmware, chunk=chunk)
    return FirmwareTerminal(serial=serial, timeout=0.5), firmware


def timed_terminal(**kwargs):
    clock = Clock()
    terminal, firmware = make_terminal(clock=clock, sleep=clock.sleep, **kwargs)
    return terminal, firmware, clock


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def wba():
    return make_terminal(family="stm32wba55")


# framing and general commands


@pytest.mark.parametrize("style", ["line", "trace"])
@pytest.mark.parametrize("chunk", [0, 3])
@pytest.mark.parametrize("noise", [False, True])
def test_command_framing(style, chunk, noise):
    terminal, firmware = make_terminal(style, chunk, noise)
    boot = terminal.wait_boot(1.0)
    assert boot["board"] == "NUCLEO-WB55RG"
    assert boot["family"] == "stm32wb55"
    assert boot["reset"] == "pin"
    assert terminal.command("ping").ok
    terminal.command("gpio.cfg led0 in")
    firmware.gpio_levels["PB0"] = 1
    assert terminal.command("gpio.get PB0").as_int("value") == 1
    info = terminal.command("info")
    assert info["board"] == "NUCLEO-WB55RG"
    assert info["sysclk"] == "64000000"
    assert len(info["uid"]) == 24


@pytest.mark.parametrize(
    ("family", "board", "sysclk"), [("stm32wb55", "NUCLEO-WB55RG", 64_000_000), ("stm32wba55", "NUCLEO-WBA55CG", 100_000_000)]
)
def test_profiles(family, board, sysclk):
    terminal, firmware = make_terminal(family=family)
    info = terminal.command("info")
    assert (info["board"], info["family"], info.as_int("sysclk")) == (board, family, sysclk)
    assert firmware.pins == (WB55_PINS if family == "stm32wb55" else WBA55_PINS)
    pins = terminal.command("board.pins").raw
    assert pins.startswith("OK terminaltx=PB6,terminalrx=PB7" if family == "stm32wb55" else "OK terminaltx=PB12,terminalrx=PA8")
    with pytest.raises(ValueError):
        FakeFirmware(family="stm32f4")


def test_board_overrides_profile_defaults():
    terminal, firmware = make_terminal(board="custom", sysclk=0, pins={"terminaltx": "PB6", "terminalrx": "PB7"})
    assert firmware.sysclk == 64_000_000
    assert terminal.command("board.pins").raw == "OK terminaltx=PB6,terminalrx=PB7"
    assert terminal.command("info")["board"] == "custom"


def test_general_command_shapes():
    terminal, _, clock = timed_terminal()
    assert reason(terminal, "ping extra") == "usage"
    assert reason(terminal, "ping nosuchkey=1") == "usage"
    assert reason(terminal, "info extra") == "usage"
    assert reason(terminal, "delay") == "usage"
    assert reason(terminal, "delay abc") == "usage"
    assert reason(terminal, "delay -5") == "usage"
    assert reason(terminal, "delay 600001") == "range"
    start = clock.now
    assert reason(terminal, "delay 0x64") == "ok"
    assert clock.now - start == pytest.approx(0.1)


def test_unknown_command_is_usage():
    """EMIL's `HilTerminal` answers `ERR usage` to unknown commands."""
    terminal, _ = make_terminal()
    with pytest.raises(FirmwareError) as error:
        terminal.command("nosuch.command")
    assert error.value.reason == "usage"


def test_reset_reports_sw_and_closes_instances():
    terminal, firmware = make_terminal("trace")
    terminal.wait_boot(1.0)
    terminal.command("spi.open 1 clk=PA5 mosi=PA7 miso=PA6")
    terminal.command("gpio.cfg gpio0 out")
    terminal.send_nowait("reset")
    boot = terminal.wait_boot(1.0)
    assert boot["reset"] == "sw"
    assert not firmware.opened
    assert not firmware.claims
    assert reason(terminal, "spi.close 1") == "notopen"


def test_sync_clears_partial_input():
    terminal, firmware = make_terminal()
    terminal.write_raw(b"gpio.se")
    terminal.sync()
    assert firmware.received[-1] == "ping"


@pytest.mark.parametrize(("family", "command"), [(family, command) for family, names in UNSUPPORTED_COMMANDS.items() for command in names])
def test_unsupported_groups(family, command):
    terminal, _ = make_terminal(family=family)
    assert reason(terminal, command) == "unsupported"
    assert reason(terminal, f"{command} 0 key=1") == "unsupported"


def test_unsupported_names_follow_the_mcu():
    """HSEM, QUADSPI, the wireless-stack flash steps, MCO and HSI48 exist on the WB55 only, LPTIM PWM on the WBA55
    only; EMIL's EEPROM group is served on both."""
    assert {"hsem.take", "qspi.open", "flash.stack", "clock.mco", "clock.hsi48"} <= set(UNSUPPORTED_COMMANDS["stm32wba55"])
    assert {"lptpwm.open", "lptpwm.close"} <= set(UNSUPPORTED_COMMANDS["stm32wb55"])
    common = set(UNSUPPORTED_COMMANDS["stm32wb55"]) & set(UNSUPPORTED_COMMANDS["stm32wba55"])
    assert {name.split(".")[0] for name in common} == {"comp", "can", "eth", "dac"}
    assert not any(name.startswith("eeprom.") for names in UNSUPPORTED_COMMANDS.values() for name in names)
    terminal, _ = make_terminal()
    assert reason(terminal, "hsem.take 0 procid=1") != "unsupported"
    terminal, _ = wba()
    assert reason(terminal, "lptpwm.close 1") != "unsupported"


# board profiles against the board files


@pytest.mark.parametrize("family", sorted(BOARDS))
def test_board_file_system_lines(family):
    """The system test lines of the board file get the answers PROTOCOL.md promises."""
    board = load_board(BOARDS[family])
    terminal, _ = make_terminal(family=family)
    for line in board.param("system.missing_instances"):
        assert reason(terminal, line) == "range", line
    for line in board.param("system.unsupported_instances"):
        assert reason(terminal, line) == "unsupported", line
    for pin in board.param("system.unbonded_pins"):
        assert reason(terminal, f"gpio.cfg {pin} in") == "pin", pin
    for pin in [*board.param("system.reserved_pins"), board.param("system.debug_led"), *board.terminal.pins]:
        assert reason(terminal, f"gpio.cfg {pin} in") == "busy", pin
    for index in board.param("system.reserved_uarts"):
        assert reason(terminal, f"uart.open {index}") == "busy"
    for line in board.param("unsupported.commands"):
        assert reason(terminal, line) == "unsupported", line


@pytest.mark.parametrize("family", sorted(BOARDS))
def test_every_alias_is_a_pin(family):
    """Every alias names a pin; the reserved ones (terminal, debug LED) answer busy."""
    board = load_board(BOARDS[family])
    terminal, firmware = make_terminal(family=family)
    for alias, pin in board.pins.items():
        expected = "busy" if pin in firmware.reserved() else "ok"
        assert reason(terminal, f"gpio.cfg {alias} in") == expected, alias
        if expected == "ok":
            assert reason(terminal, f"gpio.get {pin}") == "ok"
            assert reason(terminal, f"gpio.release {alias}") == "ok"


def test_pin_syntax():
    terminal, _ = make_terminal()
    assert reason(terminal, "gpio.cfg pa8 in") == "ok", "port letters are case-insensitive"
    assert reason(terminal, "gpio.release PA8") == "ok"
    assert reason(terminal, "gpio.cfg PA08 in") == "pin", "no leading zero"
    assert reason(terminal, "gpio.cfg PA16 in") == "pin"
    assert reason(terminal, "gpio.cfg PF0 in") == "pin", "no port F on the WB55"
    assert reason(terminal, "gpio.cfg LED0 in") == "pin", "aliases are case-sensitive"
    assert reason(terminal, "gpio.cfg nosuchalias in") == "pin"
    assert reason(terminal, "gpio.cfg PE0 in") == "pin", "PE0 is not bonded out on the VFQFPN68"
    assert reason(terminal, "gpio.get PE0") == "notopen", "the name parses; the pin is just not configured"
    assert reason(terminal, "gpio.cfg PE0 sideways") == "usage", "the mode is checked before the pin is claimed"
    terminal, _ = wba()
    assert reason(terminal, "gpio.cfg PD0 in") == "pin", "no port D on the WBA55"
    assert reason(terminal, "gpio.cfg PA4 in") == "pin", "PA4 is not bonded out on the UFQFPN48"
    assert reason(terminal, "gpio.cfg PC13 in") == "ok"


# gpio


def test_gpio_configuration():
    terminal, firmware = make_terminal()
    assert reason(terminal, "gpio.cfg PA8") == "usage"
    assert reason(terminal, "gpio.cfg PA8 sideways") == "usage"
    assert reason(terminal, "gpio.cfg PA8 in pull=sideways") == "usage"
    assert reason(terminal, "gpio.cfg PA8 out drive=8") == "usage"
    assert reason(terminal, "gpio.cfg PA8 od pull=up") == "usage"
    assert reason(terminal, "gpio.cfg PA8 od pull=none") == "ok"
    assert terminal.command("gpio.get PA8").as_int("value") == 1, "open drain starts released"
    assert reason(terminal, "gpio.cfg PA8 out drive=high") == "ok", "reconfiguring a pin is allowed"
    assert terminal.command("gpio.get PA8").as_int("value") == 0, "out starts low"
    assert reason(terminal, "gpio.set PA8 2") == "usage"
    assert reason(terminal, "gpio.set PA8 1") == "ok"
    assert terminal.command("gpio.get PA8").as_int("value") == 1
    assert reason(terminal, "gpio.pulse PA8 3 0") == "range"
    assert reason(terminal, "gpio.pulse PA8 3 10") == "ok"
    assert terminal.command("gpio.get PA8").as_int("value") == 0, "an odd number of toggles"
    assert reason(terminal, "gpio.cfg PA9 in pull=up") == "ok"
    assert terminal.command("gpio.get PA9").as_int("value") == 1
    assert reason(terminal, "gpio.pulse PA9 1 1") == "usage", "pulse needs an output"
    firmware.gpio_counts["PA9"] = 7
    assert terminal.command("gpio.count PA9 clear=1").as_int("count") == 7
    assert terminal.command("gpio.count PA9").as_int("count") == 0
    assert reason(terminal, "gpio.count PA9 clear=2") == "range"
    assert reason(terminal, "gpio.release PA9") == "ok"
    for line in ("gpio.get PA9", "gpio.set PA9 1", "gpio.count PA9", "gpio.irq PA9 rising", "gpio.release PA9"):
        assert reason(terminal, line) == "notopen", line


def test_gpio_limit_is_eight_pins():
    terminal, _ = make_terminal()
    for index in range(8):
        assert reason(terminal, f"gpio.cfg PA{index} in") == "ok"
    assert reason(terminal, "gpio.cfg PA8 in") == "busy"
    assert reason(terminal, "gpio.cfg PA0 out") == "ok", "reconfiguring needs no new entry"
    assert reason(terminal, "gpio.release PA1") == "ok"
    assert reason(terminal, "gpio.cfg PA8 in") == "ok"


def test_gpio_exti_line_serves_one_port():
    terminal, _ = make_terminal()
    terminal.command("gpio.cfg PC6 in")
    terminal.command("gpio.cfg PA6 in")
    assert reason(terminal, "gpio.irq PC6 sideways") == "usage"
    assert reason(terminal, "gpio.irq PC6 rising type=later") == "usage"
    assert reason(terminal, "gpio.irq PC6 rising") == "ok"
    assert reason(terminal, "gpio.irq PC6 both type=immediate") == "ok", "the owner can rearm"
    assert reason(terminal, "gpio.irq PA6 rising") == "unsupported"
    assert reason(terminal, "gpio.irq PA6 off") == "unsupported", "the line is checked whatever the edge"
    assert reason(terminal, "gpio.irq PC6 off") == "ok"
    assert reason(terminal, "gpio.irq PA6 falling") == "ok"
    assert reason(terminal, "gpio.irq PC6 rising") == "unsupported"
    assert reason(terminal, "gpio.cfg PA6 out") == "ok", "reconfiguring disables the interrupt"
    assert reason(terminal, "gpio.irq PC6 rising") == "ok"
    assert reason(terminal, "gpio.release PC6") == "ok", "releasing frees the line"
    assert reason(terminal, "gpio.irq PA6 rising") == "ok"


def test_gpio_port_h_has_interrupt():
    """PH3 (BOOT0) is the only port H pin and is reserved, so the rule is checked on the model."""
    _, firmware = make_terminal()
    assert firmware.supports_interrupt("PH3")
    assert firmware.supports_interrupt("PC13")


def test_gpio_pins_held_by_other_groups():
    terminal, _ = make_terminal()
    terminal.command("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 cs=PA4")
    assert reason(terminal, "gpio.cfg PA4 in") == "busy"
    assert reason(terminal, "gpio.cfg spi1clk in") == "busy"
    terminal.command("spi.close 1")
    assert reason(terminal, "gpio.cfg PA4 in") == "ok"


# pwm


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("pwm.open 1", "usage"),
        ("pwm.open", "usage"),
        ("pwm.open 1 channels=1 nosuchkey=1", "usage"),
        ("pwm.open 0 channels=1", "range"),
        ("pwm.open 18 channels=1", "range"),
        ("pwm.open 3 channels=1", "range"),
        ("pwm.open 1 channels=0", "range"),
        ("pwm.open 1 channels=5", "range"),
        ("pwm.open 1 channels=1,1", "usage"),
        ("pwm.open 1 channels=1,2,3,4,1", "usage"),
        ("pwm.open 1 channels=1,2 pins=PA8", "usage"),
        ("pwm.open 1 pins=-:-", "usage"),
        ("pwm.open 1 pins=PA8:PA7:PB8", "usage"),
        ("pwm.open 1 pins=PB0", "pin"),
        ("pwm.open 1 channels=2 pins=PA8", "pin"),
        ("pwm.open 1 pins=PA8:PB8", "pin"),
        ("pwm.open 1 pins=PA8,PD14", "pin"),
        ("pwm.open 1 pins=PA8,PA8", "usage"),
        ("pwm.open 16 channels=2", "unsupported"),
        ("pwm.open 1 channels=4 pins=PA11:PB13", "unsupported"),
        ("pwm.open 1 pins=PA8,,PA9", "ok"),
        ("pwm.open 1 channels=1,,2", "ok"),
        ("pwm.open 1 channels=", "usage"),
        ("pwm.open 1 pins=:PA7", "pin"),
        ("pwm.open 2 channels=1 brkpol=low brkauto=1", "ok"),
        ("pwm.open 2 channels=1 dead=0", "unsupported"),
        ("pwm.open 1 channels=1 brk=PA0", "pin"),
        ("pwm.open 1 channels=1 freq=0", "range"),
        ("pwm.open 1 channels=1 freq=976", "range"),
        ("pwm.open 1 channels=1 freq=977", "ok"),
        ("pwm.open 1 channels=1 freq=100 prescaler=9", "ok"),
        ("pwm.open 1 channels=1 freq=32000001", "range"),
        ("pwm.open 1 channels=1 mode=center freq=488", "range"),
        ("pwm.open 1 channels=1 mode=sideways", "usage"),
        ("pwm.open 1 channels=1 prescaler=65536", "range"),
        ("pwm.open 2 channels=1 freq=100", "ok"),
        ("pwm.open 2 channels=1 freq=1", "ok"),
        ("pwm.open 1 channels=1 dead=1000000", "ok"),
        ("pwm.open 1 channels=1 dead=1000001", "range"),
        ("pwm.open 1 channels=1 dead=fast", "usage"),
        ("pwm.open 1 channels=1 inv=2", "range"),
        ("pwm.open 1 channels=1 brkpol=sideways", "usage"),
        ("pwm.open 1 pins=PA8:PA7,PA9:PB8,PA10:PB9 dead=500 inv=1 invn=1 idle=1 idlen=1 brk=PB12 brkpol=low brkauto=1", "ok"),
        ("pwm.open 1 pins=-:PB13", "ok"),
        ("pwm.open 2 channels=1 dead=100", "unsupported"),
        ("pwm.open 2 channels=1 idle=1", "unsupported"),
        ("pwm.open 2 channels=1 dead=off idle=0 idlen=0", "ok"),
        ("pwm.open 16 channels=1 mode=center", "unsupported"),
        ("pwm.open 17 pins=PB9 brk=PA10 dead=100", "ok"),
        ("pwm.open 16 pins=PA6 brk=PB5", "busy"),
        ("pwm.open 17 pins=PB9:PB7", "busy"),
        ("pwm.open 1 channels=1,2,3,4 sync=1", "ok"),
    ],
)
def test_pwm_open_validation_wb55(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("pwm.open 3 channels=1,2,3,4", "ok"),
        ("pwm.open 3 pins=PA10:PB2", "unsupported"),
        ("pwm.open 3 pins=-:PB2", "unsupported"),
        ("pwm.open 2 channels=2", "pin"),
        ("pwm.open 2 pins=PA8", "busy"),
        ("pwm.open 2 pins=PA5", "ok"),
        ("pwm.open 1 channels=1 freq=1000", "range"),
        ("pwm.open 1 channels=1 freq=1000 mode=center", "ok"),
        ("pwm.open 17 pins=PA1:PB3 brk=PA15 dead=20000", "ok"),
        ("pwm.open 16 pins=PB9 brk=PB15", "ok"),
        ("pwm.open 4 channels=1", "range"),
        ("pwm.open 16 channels=1,2", "unsupported"),
        ("pwm.open 1 channels=4 pins=PB3:PB2", "unsupported"),
    ],
)
def test_pwm_open_validation_wba55(line, expected):
    """WBA55: TIM3 exists; TIM2 CH2 is only the terminal RX pin (reserved: busy)."""
    terminal, _ = wba()
    assert reason(terminal, line) == expected


def test_pwm_duties_follow_the_command_order():
    terminal, firmware = make_terminal()
    terminal.command("pwm.open 1 pins=PA10,PA8")
    assert [channel for channel, _, _ in firmware.opened[("pwm", "1")]["channels"]] == [3, 1]
    terminal.command("pwm.duty 1 30 10")
    assert firmware.opened[("pwm", "1")]["duties"] == [30, 10]


def test_pwm_channels_without_pins_take_the_first_free_table_pin():
    terminal, firmware = make_terminal()
    terminal.command("pwm.open 2 channels=4,1")
    assert firmware.opened[("pwm", "2")]["channels"] == [(4, "PA3", None), (1, "PA0", None)]
    terminal.command("pwm.close 2")
    terminal.command("pwm.open 17 channels=1")
    assert firmware.opened[("pwm", "17")]["channels"] == [(1, "PA7", None)]


@pytest.mark.parametrize("prescaler", [0, 1, 63, 999, 65535])
@pytest.mark.parametrize("freq", [100, 1000, 10000, 1000000])
@pytest.mark.parametrize("mode", ["edge", "center"])
@pytest.mark.parametrize("timer", [1, 2])
def test_pwm_range_matches_expect(prescaler, freq, mode, timer):
    terminal, _ = make_terminal()
    pwmclk = expect.pwm_clock(64_000_000, prescaler)
    fits = expect.pwm_fits(pwmclk, freq, mode, expect.timer_counter_max(timer))
    response = terminal.command(f"pwm.open {timer} channels=1 freq={freq} mode={mode} prescaler={prescaler}", check=False)
    assert response.ok is fits
    if fits:
        assert response.as_int("pwmclk") == pwmclk


def test_pwm_duty_frequency_stop_close():
    terminal, firmware = make_terminal()
    assert reason(terminal, "pwm.duty 1 50") == "notopen"
    assert reason(terminal, "pwm.duty 99 50") == "range"
    assert terminal.command("pwm.open 1 channels=1,2 prescaler=63")["pwmclk"] == "1000000"
    assert reason(terminal, "pwm.duty 1") == "usage"
    assert reason(terminal, "pwm.duty 1 10 20 30") == "usage", "one duty per channel or one for all"
    for duty, expected in (
        ("100.5", "usage"),
        ("101", "usage"),
        ("12.34567", "usage"),
        ("0x10", "usage"),
        ("-1", "usage"),
        (".5", "usage"),
    ):
        assert reason(terminal, f"pwm.duty 1 {duty}") == expected, duty
    assert reason(terminal, "pwm.duty 1 12.5 100") == "ok"
    assert reason(terminal, "pwm.duty 1 0.0001") == "ok"
    assert firmware.opened[("pwm", "1")]["running"]
    assert reason(terminal, "pwm.freq 1 0") == "range"
    assert reason(terminal, "pwm.freq 1 15") == "range", "1 MHz / 15 Hz overflows the 16-bit counter"
    assert reason(terminal, "pwm.freq 1 16") == "ok"
    assert reason(terminal, "pwm.freq 1 500001") == "range"
    assert reason(terminal, "pwm.freq 1 500000") == "ok"
    assert reason(terminal, "pwm.stop 1") == "ok"
    assert not firmware.opened[("pwm", "1")]["running"]
    assert reason(terminal, "pwm.close 2") == "notopen"
    assert reason(terminal, "pwm.close 1") == "ok"
    assert reason(terminal, "pwm.close 1") == "notopen"


def test_one_pwm_timer_at_a_time():
    terminal, _ = make_terminal()
    terminal.command("pwm.open 16 channels=1")
    assert reason(terminal, "pwm.open 1 channels=1") == "busy"
    assert reason(terminal, "pwm.open 16 channels=1") == "busy"
    assert reason(terminal, "pwm.open 3 channels=1") == "range", "argument errors come before busy"
    assert reason(terminal, "pwm.open 1 channels=5") == "range"
    assert reason(terminal, "pwm.open 1 pins=PB0") == "pin"
    assert reason(terminal, "pwm.open 2 channels=1 dead=100") == "unsupported"


def test_timer_shared_between_groups():
    """A timer serves one of PWM, encoder and timer-triggered ADC at a time."""
    terminal, _ = make_terminal()
    terminal.command("qei.open 2")
    assert reason(terminal, "pwm.open 2 channels=3") == "busy"
    assert reason(terminal, "adc.open 1 pins=PC3 timer=2") == "busy"
    assert reason(terminal, "adc.open 1 pins=PC3 timer=1") == "ok"
    assert reason(terminal, "pwm.open 1 channels=4") == "busy"
    assert reason(terminal, "pwm.open 16 channels=1") == "ok"
    terminal.command("qei.close 2")
    terminal.command("pwm.close 16")
    assert reason(terminal, "pwm.open 2 channels=3") == "ok"
    assert reason(terminal, "qei.open 1 a=PA8 b=PA9") == "busy", "TIM1 triggers the ADC"
    terminal.command("adc.close 1")
    assert reason(terminal, "qei.open 1 a=PA8 b=PA9") == "ok"


# uart


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("uart.open", "usage"),
        ("uart.open 1 lp=1 nosuchkey=1", "usage"),
        ("uart.open 1", "busy"),
        ("uart.open 1 baud=1", "range"),
        ("uart.open 1 parity=mark", "usage"),
        ("uart.open 1 tx=PB0 rx=PB0", "pin"),
        ("uart.open 1 tx=PB6", "usage"),
        ("uart.open 1 tx=PA9 rx=PA10", "busy"),
        ("uart.open 1 dma=1", "busy"),
        ("uart.open 2", "range"),
        ("uart.open 0 lp=1", "range"),
        ("uart.open 2 lp=1", "range"),
        ("uart.open 1 lp=2", "range"),
        ("uart.open 1 lp=1", "ok"),
        ("uart.open 1 lp=1 baud=fast", "usage"),
        ("uart.open 1 lp=1 baud=299", "range"),
        ("uart.open 1 lp=1 baud=15625", "range"),
        ("uart.open 1 lp=1 baud=15626", "ok"),
        ("uart.open 1 lp=1 baud=8000000", "ok"),
        ("uart.open 1 lp=1 baud=8000001", "range"),
        ("uart.open 1 lp=1 baud=12000000", "range"),
        ("uart.open 1 lp=1 baud=12000001", "range"),
        ("uart.open 1 lp=1 parity=mark", "usage"),
        ("uart.open 1 lp=1 parity=odd", "ok"),
        ("uart.open 1 lp=1 stop=2", "usage"),
        ("uart.open 1 lp=1 flow=sideways", "usage"),
        ("uart.open 1 lp=1 dma=1 duplex=1", "usage"),
        ("uart.open 1 lp=1 dma=1", "ok"),
        ("uart.open 1 lp=1 duplex=1", "unsupported"),
        ("uart.open 1 lp=1 sync=1", "unsupported"),
        ("uart.open 1 lp=1 swap=1", "ok"),
        ("uart.open 1 lp=1 tx=PA2", "usage"),
        ("uart.open 1 lp=1 tx=PB11 rx=PB10", "ok"),
        ("uart.open 1 lp=1 tx=PB5 rx=PB10", "busy"),
        ("uart.open 1 lp=1 tx=PB6 rx=PB7", "pin"),
        ("uart.open 1 lp=1 tx=PA3 rx=PA2", "pin"),
        ("uart.open 1 lp=1 flow=rtscts", "usage"),
        ("uart.open 1 lp=1 flow=rtscts rts=PB12", "usage"),
        ("uart.open 1 lp=1 rts=PB12", "usage"),
        ("uart.open 1 lp=1 flow=rts", "usage"),
        ("uart.open 1 lp=1 flow=rtscts rts=PB12 cts=PA6", "usage"),
        ("uart.open 1 lp=1 tx=PA2 rx=PA3 flow=rtscts rts=PB12 cts=PA6", "ok"),
        ("uart.open 1 lp=1 tx=PA2 rx=PA3 flow=rtscts rts=PB12 cts=PA11", "pin"),
        ("uart.open 1 lp=1 flow=rts rts=PB12", "unsupported"),
        ("uart.open 1 lp=1 flow=cts cts=PA6", "unsupported"),
        ("uart.open 1 lp=1 tx=PB11 rx=PB10 rts=PB1 flow=rtscts cts=PB13", "ok"),
        ("uart.open 1 lp=1 tx=PB5 rx=PB5", "pin"),
        ("uart.open 1 lp=1 tx=PA2 rx=PA12 rts=PB12 cts=PB13 flow=rtscts", "ok"),
    ],
)
def test_uart_open_validation_wb55(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("uart.open 1", "busy"),
        ("uart.open 2", "usage"),
        ("uart.open 3", "range"),
        ("uart.open 2 tx=PB0 rx=PA11", "ok"),
        ("uart.open 2 tx=PA11 rx=PB0", "pin"),
        ("uart.open 2 tx=PB0 rx=PA11 baud=3051", "range"),
        ("uart.open 2 tx=PB0 rx=PA11 baud=3052", "ok"),
        ("uart.open 1 lp=1 baud=24414", "range"),
        ("uart.open 1 lp=1 baud=24415", "ok"),
        ("uart.open 2 tx=PB0 rx=PA11 duplex=1", "ok"),
        ("uart.open 2 tx=PB0 rx=PA11 sync=1", "ok"),
        ("uart.open 2 tx=PB0 rx=PA11 sync=1 parity=even", "unsupported"),
        ("uart.open 2 tx=PB0 rx=PA11 sync=1 swap=1", "unsupported"),
        ("uart.open 2 tx=PB0 rx=PA11 dma=1 sync=1", "usage"),
        ("uart.open 2 tx=PB0 rx=PA11 sync=1 flow=rts", "usage"),
        ("uart.open 2 tx=PB0 rx=PA11 sync=1 flow=rts rts=PB1 cts=PB2", "usage"),
        ("uart.open 2 tx=PB0 rx=PA11 flow=cts", "usage"),
        ("uart.open 2 tx=PB0 rx=PA11 baud=12000000", "ok"),
        ("uart.open 2 tx=PB0 rx=PA11 sync=1 flow=rts rts=PB1", "ok"),
        ("uart.open 2 tx=PB0 rx=PA11 sync=1 flow=cts cts=PB2", "ok"),
        ("uart.open 2 tx=PB0 rx=PA11 flow=rtscts rts=PB1 cts=PB2 dma=1", "ok"),
        ("uart.open 2 tx=PB0 rx=PA11 flow=rts rts=PB1", "unsupported"),
        ("uart.open 2 tx=PB0 rx=PA11 flow=rtscts rts=PB2 cts=PB1", "pin"),
        ("uart.open 2 tx=PB0 rx=PA11 swap=1", "ok"),
        ("uart.open 1 lp=1", "ok"),
        ("uart.open 1 lp=1 tx=PA2 rx=PA1", "ok"),
    ],
)
def test_uart_open_validation_wba55(line, expected):
    terminal, _ = wba()
    assert reason(terminal, line) == expected


def test_uart_send_and_receive():
    terminal, firmware = make_terminal()
    assert reason(terminal, "uart.send 1 55") == "notopen"
    assert reason(terminal, "uart.recv 1") == "notopen"
    terminal.command("uart.open 1 lp=1")
    assert reason(terminal, "uart.open 1 lp=1") == "busy"
    assert reason(terminal, "uart.send 1") == "usage"
    assert reason(terminal, "uart.send 1 -") == "usage"
    assert reason(terminal, "uart.send 1 5") == "usage"
    assert reason(terminal, "uart.send 1 zz") == "usage"
    assert reason(terminal, "uart.send 1 " + "00" * 112) == "ok"
    assert reason(terminal, "uart.send 1 " + "00" * 113) == "range"
    assert reason(terminal, "uart.recv 1 timeout=10001") == "range"
    assert reason(terminal, "uart.recv 1 len=0") == "range"
    assert reason(terminal, "uart.recv 1 len=257") == "range"
    assert reason(terminal, "uart.recv 1 sideways=1") == "usage"
    firmware.uart_rx[1] += bytes(range(10))
    assert terminal.command("uart.recv 1 len=4 timeout=0").as_bytes("data") == bytes(range(10))
    assert terminal.command("uart.recv 1").raw == "OK data=-"
    firmware.uart_rx[1] += b"\x55" * 300
    assert len(terminal.command("uart.recv 1").as_bytes("data")) == 256
    assert len(terminal.command("uart.recv 1").as_bytes("data")) == 44
    assert reason(terminal, "uart.close 1") == "ok"
    assert reason(terminal, "uart.close 1") == "notopen"


def test_uart_pins_and_busy_ordering():
    terminal, _ = make_terminal()
    terminal.command("gpio.cfg PA2 in")
    assert reason(terminal, "uart.open 1 lp=1 tx=PA3 rx=PA2") == "pin", "argument errors before busy"
    assert reason(terminal, "uart.open 1 lp=1") == "busy", "PA2 is held by the GPIO group"
    terminal.command("gpio.release PA2")
    terminal.command("spi.open 1 clk=PA5 mosi=PA7 miso=PA6")
    assert reason(terminal, "uart.open 1 lp=1 flow=rtscts rts=PB12 cts=PA6") == "usage", "rts and cts are pins: no defaults"
    assert reason(terminal, "uart.open 1 lp=1 tx=PA2 rx=PA3 flow=rtscts rts=PB12 cts=PA6") == "busy"
    assert reason(terminal, "uart.open 1 lp=1 tx=PA2 rx=PA3 flow=rtscts rts=PB12 cts=PB13") == "ok"


# spi


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("spi.open 1", "usage"),
        ("spi.open 1 clk=PA5 mosi=PA7", "usage"),
        ("spi.open 0 clk=PA5 mosi=PA7 miso=PA6", "range"),
        ("spi.open 3 clk=PA5 mosi=PA7 miso=PA6", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 cs=PC13", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 cs=PB6", "busy"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 cs=PA5", "busy"),
        ("spi.open 1 clk=PA6 mosi=PA7 miso=PA5", "pin"),
        ("spi.open 1 clk=PB13 mosi=PA7 miso=PA6", "pin"),
        ("spi.open 2 clk=PB13 mosi=PB15 miso=PB14", "ok"),
        ("spi.open 2 clk=PD3 mosi=PB15 miso=PB14", "pin"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 baud=249999", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 baud=250000", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 baud=32000000", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 baud=32000001", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 mode=4", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 dma=1 sync=1", "usage"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 baud=0", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 baud=1", "usage"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 cs=PE0", "pin"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 cs=PA16", "pin"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 sync=1 cs=PA4", "ok"),
    ],
)
def test_spi_open_validation_wb55(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


def test_spi_wba55_instances_and_clock():
    terminal, firmware = wba()
    assert reason(terminal, "spi.open 2 clk=PB4 mosi=PA15 miso=PB3") == "range", "no SPI2 on the WBA55"
    assert reason(terminal, "spi.open 1 clk=PB4 mosi=PA15 miso=PB3 baud=390624") == "range"
    assert reason(terminal, "spi.open 1 clk=PB4 mosi=PA15 miso=PB3 baud=3000000") == "ok"
    assert firmware.opened[("spi", "1")]["clock"] == 1_562_500
    terminal.command("spi.close 1")
    assert reason(terminal, "spi.open 3 clk=PA0 mosi=PB8 miso=PB9") == "ok", "PB8 is free since the debug LED moved to PA9"


def test_spi_transfers():
    terminal, firmware = make_terminal()
    assert reason(terminal, "spi.xfer 1 00") == "notopen"
    terminal.command("spi.open 1 clk=PA5 mosi=PA7 miso=PA6")
    assert reason(terminal, "spi.open 2 clk=PB13 mosi=PB15 miso=PB14") == "busy", "one SPI at a time"
    assert reason(terminal, "spi.xfer 1 -") == "usage"
    assert reason(terminal, "spi.xfer 1 - rx=0") == "usage"
    assert reason(terminal, "spi.xfer 1 123") == "usage"
    assert reason(terminal, "spi.xfer 1 " + "00" * 65) == "range"
    assert reason(terminal, "spi.xfer 1 - rx=65") == "range"
    assert reason(terminal, "spi.xfer 1 00 continue=2") == "range"
    firmware.spi_miso = 0xA5
    assert terminal.command("spi.xfer 1 " + "00" * 64).as_bytes("rx") == b"\xa5" * 64
    assert terminal.command("spi.xfer 1 0102 rx=4").as_bytes("rx") == b"\xa5" * 4
    assert terminal.command("spi.xfer 1 0102 rx=0").raw == "OK rx=-"


# adc


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("adc.open 1", "usage"),
        ("adc.open 1 pins=PC3 nosuchkey=1", "usage"),
        ("adc.open 0 pins=PC3", "range"),
        ("adc.open 2 pins=PC3", "range"),
        ("adc.open 1 pins=PC3", "ok"),
        ("adc.open 1 pins=ain4,ain3,ain1,ain2,ain5,ain6,PA2,PA3", "ok"),
        ("adc.open 1 pins=ain4,ain3,ain1,ain2,ain5,ain6,PA2,PA3,PA4", "range"),
        ("adc.open 1 pins=PC3,PC3,PC2,PC3", "ok"),
        ("adc.open 1 pins=PB0", "pin"),
        ("adc.open 1 pins=PC3,PC6", "pin"),
        ("adc.open 1 pins=PC3 sampling=2.5", "ok"),
        ("adc.open 1 pins=PC3 sampling=640.5", "ok"),
        ("adc.open 1 pins=PC3 sampling=3.5", "usage"),
        ("adc.open 1 pins=PC3 rate=1000", "usage"),
        ("adc.open 1 pins=PC3 timer=1", "ok"),
        ("adc.open 1 pins=PC3 timer=2 rate=100000", "ok"),
        ("adc.open 1 pins=PC3 timer=2 rate=100001", "range"),
        ("adc.open 1 pins=PC3 timer=2 rate=0", "range"),
        ("adc.open 1 pins=PC3 timer=3", "range"),
        ("adc.open 1 pins=PC3 timer=16", "unsupported"),
        ("adc.open 1 pins=PC3 timer=18", "range"),
    ],
)
def test_adc_open_validation_wb55(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("adc.open 1 pins=PA7", "range"),
        ("adc.open 4 pins=PA7,PA6,PB9", "ok"),
        ("adc.open 4 pins=PA8", "busy"),
        ("adc.open 4 pins=PA4", "pin"),
        ("adc.open 4 pins=PB0", "pin"),
        ("adc.open 4 pins=PA7 sampling=814.5", "ok"),
        ("adc.open 4 pins=PA7 sampling=2.5", "usage"),
        ("adc.open 4 pins=PA7 timer=3", "unsupported"),
        ("adc.open 4 pins=PA7 timer=2 rate=500", "ok"),
    ],
)
def test_adc_open_validation_wba55(line, expected):
    terminal, _ = wba()
    assert reason(terminal, line) == expected


def test_adc_measure_software_trigger():
    terminal, firmware, clock = timed_terminal()
    assert reason(terminal, "adc.measure 1") == "notopen"
    terminal.command("adc.open 1 pins=ain4,ain3")
    assert reason(terminal, "adc.open 1 pins=ain1") == "busy", "one ADC at a time"
    firmware.adc_codes.update(PC3=100, PC2=3000)
    start = clock.now
    assert terminal.command("adc.measure 1 n=3").as_ints("samples") == [100, 3000] * 3
    assert clock.now - start == pytest.approx(expect.terminal_line_time("OK samples=100,3000,100,3000,100,3000", 921600))
    assert reason(terminal, "adc.measure 1 n=32") == "ok"
    assert reason(terminal, "adc.measure 1 n=33") == "range", "at most 64 values"
    assert reason(terminal, "adc.measure 1 n=0") == "range"
    assert reason(terminal, "adc.measure 1 n=65") == "range"
    assert reason(terminal, "adc.measure 0") == "range", "the ADC number is checked before the slot"
    assert reason(terminal, "adc.measure 2") == "range"
    assert reason(terminal, "adc.close 1") == "ok"


def test_adc_timer_trigger_rate():
    terminal, firmware, clock = timed_terminal()
    terminal.command("adc.open 1 pins=ain4 timer=2 rate=50")
    firmware.adc_codes["PC3"] = 1234
    start = clock.now
    assert terminal.command("adc.measure 1 n=25").as_ints("samples") == [1234] * 25
    reply = "OK samples=" + ",".join(["1234"] * 25)
    assert clock.now - start == pytest.approx(0.5 + expect.terminal_line_time(reply, 921600)), "the reply is sent at 921600 Bd"
    start = clock.now
    assert reason(terminal, "adc.measure 1 n=64") == "timeout", "64 runs at 50 Hz take longer than 1 s"
    assert clock.now - start == pytest.approx(1.0)
    terminal.command("adc.close 1")
    terminal.command("adc.open 1 pins=ain4,ain3 timer=1")
    start = clock.now
    assert len(terminal.command("adc.measure 1 n=10").as_ints("samples")) == 20
    reply = "OK samples=" + ",".join(["1234", "2048"] * 10)
    assert clock.now - start == pytest.approx(10 / expect.ADC_DEFAULT_RATE + expect.terminal_line_time(reply, 921600))


def test_adc_pins_are_shared_only_with_analog_users():
    terminal, _ = make_terminal()
    terminal.command("gpio.cfg PC3 in")
    assert reason(terminal, "adc.open 1 pins=PC3") == "busy"
    terminal.command("gpio.release PC3")
    terminal.command("adc.open 1 pins=PC3,PC3")
    assert reason(terminal, "gpio.cfg PC3 in") == "busy"


# qei


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("qei.open 2", "ok"),
        ("qei.open 2 res=1", "range"),
        ("qei.open 2 res=4294967295", "ok"),
        ("qei.open 2 res=4294967296", "usage"),
        ("qei.open 1 res=65537 a=PA8 b=PA9", "range"),
        ("qei.open 1 res=65536 a=PA8 b=PA9", "ok"),
        ("qei.open 2 res=100 offset=100", "range"),
        ("qei.open 2 res=100 offset=99", "ok"),
        ("qei.open 2 cap=x", "usage"),
        ("qei.open 2 cap=b", "ok"),
        ("qei.open 2 filter=16", "range"),
        ("qei.open 2 filter=15 vel=off", "ok"),
        ("qei.open 2 vel=0", "range"),
        ("qei.open 2 vel=1000001", "range"),
        ("qei.open 2 inva=1 invb=1", "ok"),
        ("qei.open 1", "usage"),
        ("qei.open 1 a=PA8", "usage"),
        ("qei.open 1 a=PA8 b=PA9", "ok"),
        ("qei.open 1 a=PA9 b=PA8", "pin"),
        ("qei.open 2 idx=PC6", "usage"),
        ("qei.open 2 a=PA15 b=PB3 idx=PC13", "ok"),
        ("qei.open 2 a=PA0 b=PA1", "ok"),
        ("qei.open 3", "range"),
        ("qei.open 16 a=PA6 b=PB8", "unsupported"),
        ("qei.open 17", "unsupported"),
        ("qei.open 18", "range"),
        ("qei.open 1 lp=1", "usage"),
        ("qei.open 1 lp=1 a=PC0 b=PC2", "ok"),
        ("qei.open 1 lp=1 a=PB5 b=PB7", "busy"),
        ("qei.open 1 lp=1 a=PC2 b=PC0", "pin"),
        ("qei.open 2 lp=1 a=PC0 b=PC2", "range"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 res=65537", "range"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 cap=a", "unsupported"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 offset=1", "unsupported"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 invb=1", "unsupported"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 offset=0", "unsupported"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 invb=0", "unsupported"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 cap=ab", "ok"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 cap=rise", "ok"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 cap=fall", "ok"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 cap=b", "unsupported"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 cap=x", "usage"),
        ("qei.open 2 cap=rise", "usage"),
        ("qei.open 2 lp=1 a=PB1 b=PC2", "range"),
        ("qei.open 0 lp=1 a=PC0 b=PC2", "range"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 inva=1 filter=8", "ok"),
        ("qei.open 1 lp=1 a=PC0 b=PC2 filter=3", "range"),
    ],
)
def test_qei_open_validation_wb55(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("qei.open 1", "ok"),
        ("qei.open 1 res=4294967296", "usage"),
        ("qei.open 1 res=65537", "range"),
        ("qei.open 3 a=PA10 b=PA1", "ok"),
        ("qei.open 3", "usage"),
        ("qei.open 2 a=PA5 b=PA8", "busy"),
        ("qei.open 1 lp=1 a=PA0 b=PB3", "ok"),
        ("qei.open 2 lp=1 a=PB9 b=PB0", "ok"),
        ("qei.open 2 lp=1 a=lptim2in1 b=lptim2in2 cap=fall filter=8", "ok"),
        ("qei.open 1 lp=1 a=PA0 b=PB3 cap=a", "unsupported"),
        ("qei.open 1 lp=1 a=PB9 b=PB0", "pin"),
        ("qei.open 1 lp=1", "usage"),
        ("qei.open 0 lp=1", "range"),
        ("qei.open 3 lp=1", "range"),
        ("qei.open 17 lp=1 filter=3", "range"),
        ("qei.open 4", "range"),
    ],
)
def test_qei_open_validation_wba55(line, expected):
    terminal, _ = wba()
    assert reason(terminal, line) == expected


def test_qei_read_index_and_close():
    terminal, firmware = make_terminal()
    assert reason(terminal, "qei.read 2") == "notopen"
    assert reason(terminal, "qei.index 2") == "notopen"
    assert reason(terminal, "qei.index 18") == "range"
    terminal.command("qei.open 2 res=1000 offset=10")
    assert reason(terminal, "qei.open 1 a=PA8 b=PA9") == "busy", "one encoder at a time"
    reading = terminal.command("qei.read 2")
    assert (reading.as_int("pos"), reading["dir"], reading.as_int("speed"), reading.as_int("res")) == (10, "fwd", 0, 1000)
    assert terminal.command("qei.index 2")["idx"] == "0"
    firmware.gpio_levels["PC6"] = 1
    assert terminal.command("qei.index 2")["idx"] == "1"
    assert reason(terminal, "qei.index 1") == "notopen"
    assert reason(terminal, "gpio.cfg qei2idx in") == "busy"
    terminal.command("qei.close 2")
    terminal.command("qei.open 2 a=PA15 b=PB3")
    assert reason(terminal, "qei.index 2") == "unsupported", "opened without idx"
    terminal.command("qei.close 2")
    terminal.command("qei.open 1 lp=1 a=PC0 b=PC2")
    assert terminal.command("qei.read 1").as_int("res") == 4096


# watchdog


@pytest.mark.parametrize(
    ("family", "line", "expected"),
    [
        ("stm32wb55", "wdt.start 0", "usage"),
        ("stm32wb55", "wdt.start 0 feed=auto", "usage"),
        ("stm32wb55", "wdt.start 0 timeout=0", "range"),
        ("stm32wb55", "wdt.start 0 timeout=30001", "range"),
        ("stm32wb55", "wdt.start 0 timeout=517", "range"),
        ("stm32wb55", "wdt.start 0 timeout=516", "ok"),
        ("stm32wb55", "wdt.start 1 timeout=100", "range"),
        ("stm32wb55", "wdt.start 0 timeout=100 feed=sometimes", "usage"),
        ("stm32wb55", "wdt.start 0 timeout=100 reset=0", "usage"),
        ("stm32wb55", "wdt.start 0 timeout=100 pin=terminaltx", "busy"),
        ("stm32wb55", "wdt.start 0 timeout=100 pin=PA16", "pin"),
        ("stm32wb55", "wdt.start 0 timeout=517 pin=terminaltx", "range"),
        ("stm32wb55", "wdt.start 0 timeout=517 pin=PE0", "range"),
        ("stm32wb55", "wdt.start 0 timeout=100 pin=PE0", "pin"),
        ("stm32wb55", "wdt.feed 0", "notopen"),
        ("stm32wb55", "wdt.feed 1", "range"),
        ("stm32wba55", "wdt.start 0 timeout=331", "range"),
        ("stm32wba55", "wdt.start 0 timeout=330 pin=gpio0", "ok"),
    ],
)
def test_watchdog_start_validation(family, line, expected):
    terminal, _, _ = timed_terminal(family=family)
    assert reason(terminal, line) == expected


def test_watchdog_auto_feed_warns_every_period_and_never_resets():
    terminal, firmware, clock = timed_terminal()
    terminal.wait_boot(0.5)
    terminal.command("wdt.start 0 timeout=20 pin=gpio0")
    assert reason(terminal, "gpio.cfg gpio0 in") == "busy", "the toggle pin stays claimed"
    assert reason(terminal, "wdt.start 0 timeout=20") == "busy"
    period = expect.wwdg_warning_period(20, 64_000_000)
    assert period == pytest.approx(0.032256)
    clock.now += 10 * period + period / 2
    terminal.pump()
    warnings = terminal.drain_events("wdt")
    assert [event.as_int("warning") for event in warnings] == list(range(1, 11))
    assert all(event.as_int("index") == 0 for event in warnings)
    assert firmware.gpio_levels["PC6"] == 0, "ten toggles"
    assert not terminal.events("boot")
    assert reason(terminal, "wdt.feed 0") == "ok"


def test_watchdog_manual_feed_then_reset():
    terminal, firmware, clock = timed_terminal()
    terminal.wait_boot(0.5)
    terminal.command("wdt.start 0 timeout=100 feed=manual")
    period = expect.wwdg_warning_period(100, 64_000_000)
    for _ in range(5):
        clock.now += period * 0.9
        terminal.command("wdt.feed 0")
    assert not terminal.drain_events("wdt")
    clock.now += period
    terminal.pump()
    assert [event.as_int("warning") for event in terminal.drain_events("wdt")] == [1]
    assert not terminal.events("boot"), "the reset comes one counter tick after the warning"
    clock.now += expect.wwdg_tick(64_000_000, 32)
    boot = terminal.wait_boot(0.5)
    assert boot["reset"] == "wwdg"
    assert firmware.watchdog is None
    assert reason(terminal, "wdt.feed 0") == "notopen"
    assert terminal.command("info")["reset"] == "wwdg"


def test_watchdog_feed_after_warning_cancels_reset():
    terminal, _, clock = timed_terminal()
    terminal.wait_boot(0.5)
    terminal.command("wdt.start 0 timeout=100 feed=manual")
    clock.now += expect.wwdg_warning_period(100, 64_000_000)
    terminal.pump()
    assert terminal.drain_events("wdt")
    terminal.command("wdt.feed 0")
    clock.now += expect.wwdg_tick(64_000_000, 32) * 2
    terminal.pump()
    assert not terminal.events("boot")


@pytest.mark.parametrize(("family", "pclk1"), [("stm32wb55", 64_000_000), ("stm32wba55", 100_000_000)])
def test_watchdog_reset_cuts_the_warning_line(family, pclk1):
    """At 5 ms the counter tick between the warning and the reset is shorter than the `EVT wdt` line: the host gets
    a fragment, then the boot banner; a feed within that tick lets the line complete."""
    terminal, _, clock = timed_terminal(family=family)
    terminal.wait_boot(0.5)
    prescaler = expect.wwdg_prescaler_for(5, pclk1)
    assert not expect.wwdg_warning_outruns_reset(5, pclk1, 921600)
    terminal.command("wdt.start 0 timeout=5 feed=manual")
    clock.now += expect.wwdg_warning_period(5, pclk1)
    terminal.pump()
    assert not terminal.events("wdt"), "the line is still being sent"
    clock.now += expect.wwdg_tick(pclk1, prescaler)
    assert terminal.wait_boot(0.5)["reset"] == "wwdg"
    assert all("warning=" not in event.raw for event in terminal.drain_events("wdt"))
    terminal.command("wdt.start 0 timeout=5 feed=manual")
    clock.now += expect.wwdg_warning_period(5, pclk1)
    terminal.pump()
    terminal.command("wdt.feed 0")
    assert [event.as_int("warning") for event in terminal.drain_events("wdt")] == [1]
    clock.now += expect.wwdg_tick(pclk1, prescaler) * 2
    terminal.pump()
    assert not terminal.events("boot")


# error ordering


def test_argument_errors_come_before_busy():
    """PROTOCOL.md: `usage`, `range`, `pin` and `unsupported` are reported before `ERR busy`."""
    terminal, _ = make_terminal()
    terminal.command("spi.open 1 clk=PA5 mosi=PA7 miso=PA6")
    terminal.command("uart.open 1 lp=1")
    terminal.command("pwm.open 1 channels=1")
    terminal.command("qei.open 2")
    terminal.command("adc.open 1 pins=PC3")
    terminal.command("wdt.start 0 timeout=500")
    cases = [
        ("spi.open 1 clk=PA5 mosi=PA7", "usage"),
        ("spi.open 3 clk=PA5 mosi=PA7 miso=PA6", "range"),
        ("spi.open 1 clk=PB0 mosi=PA7 miso=PA6", "pin"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 baud=1", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6", "busy"),
        ("uart.open 1 lp=1 baud=300", "range"),
        ("uart.open 1 lp=1 duplex=1", "unsupported"),
        ("uart.open 1 lp=1 tx=PB0 rx=PA3", "pin"),
        ("uart.open 1 lp=1", "busy"),
        ("pwm.open 2 channels=1 dead=100", "unsupported"),
        ("pwm.open 2 channels=1 freq=0", "range"),
        ("pwm.open 16 pins=PB0", "pin"),
        ("pwm.open 16 channels=1", "busy"),
        ("qei.open 16", "unsupported"),
        ("qei.open 1 a=PA9 b=PA8", "pin"),
        ("qei.open 1 a=PA8 b=PA9", "busy"),
        ("adc.open 1 pins=PB0", "pin"),
        ("adc.open 1 pins=PC2 timer=16", "unsupported"),
        ("adc.open 1 pins=PC2", "busy"),
        ("wdt.start 0 timeout=600", "range"),
        ("wdt.start 0 timeout=100 feed=never", "usage"),
        ("wdt.start 0 timeout=100", "busy"),
    ]
    for line, expected in cases:
        assert reason(terminal, line) == expected, line


# board profiles of the coverage extensions


def test_wba55_bonding_and_debug_led():
    """PA3, PA4, PB10, PB11 and PB13 are not bonded on the UFQFPN48; the debug LED is the green LD2 on PA9."""
    terminal, firmware = wba()
    for pin in ("PA3", "PA4", "PB10", "PB11", "PB13"):
        assert reason(terminal, f"gpio.cfg {pin} in") == "pin", pin
    assert reason(terminal, "gpio.cfg PA9 in") == "busy"
    assert reason(terminal, "gpio.cfg led1 in") == "busy", "led1 names the debug LED pin"
    assert reason(terminal, "gpio.cfg PB8 in") == "ok"
    assert firmware.spec.debug_led == "PA9"


@pytest.mark.parametrize(
    ("family", "function", "instance", "pin"),
    [
        ("stm32wb55", "i2cScl", 1, "PB8"),
        ("stm32wb55", "i2cSda", 1, "PB9"),
        ("stm32wb55", "i2cScl", 3, "PC0"),
        ("stm32wb55", "i2cSda", 3, "PC1"),
        ("stm32wb55", "i2cScl", 3, "PA7"),
        ("stm32wb55", "i2cSda", 3, "PB4"),
        ("stm32wb55", "spiSlaveSelect", 1, "PA4"),
        ("stm32wb55", "spiSlaveSelect", 2, "PB12"),
        ("stm32wb55", "quadSpiClock", 0, "PA3"),
        ("stm32wb55", "quadSpiSlaveSelect", 0, "PA2"),
        ("stm32wb55", "quadSpiData0", 0, "PB9"),
        ("stm32wb55", "quadSpiData3", 0, "PA6"),
        ("stm32wb55", "lpTimerInput1", 2, "PB1"),
        ("stm32wba55", "i2cScl", 1, "PB2"),
        ("stm32wba55", "i2cSda", 1, "PB1"),
        ("stm32wba55", "i2cScl", 1, "PA15"),
        ("stm32wba55", "i2cSda", 1, "PB3"),
        ("stm32wba55", "i2cScl", 3, "PA6"),
        ("stm32wba55", "i2cSda", 3, "PA7"),
        ("stm32wba55", "spiSlaveSelect", 3, "PA5"),
        ("stm32wba55", "lpTimerChannel2", 1, "PA15"),
        ("stm32wba55", "lpTimerChannel1", 2, "PA11"),
        ("stm32wba55", "lpTimerChannel2", 2, "PA1"),
        ("stm32wba55", "lpTimerInput1", 1, "PA0"),
        ("stm32wba55", "lpTimerInput2", 2, "PB0"),
        ("stm32wba55", "timerChannel1N", 16, "PB8"),
    ],
)
def test_pin_functions_of_the_new_groups(family, function, instance, pin):
    _, firmware = make_terminal(family=family)
    assert firmware.supports(function, instance, pin)


def test_unbonded_table_pins_offer_nothing():
    _, firmware = wba()
    assert not firmware.supports("lpTimerChannel1", 1, "PB11"), "LPTIM1 CH1 is on PB11 only, which is not bonded"
    assert not firmware.supports("timerChannel1N", 16, "PA3")
    assert firmware.first_function_pin("lpuartTx", 1) == "PA2"


@pytest.mark.parametrize(("family", "pins"), [("stm32wb55", WB55_PINS), ("stm32wba55", WBA55_PINS)])
def test_new_aliases_resolve(family, pins):
    terminal, _ = make_terminal(family=family)
    for alias in ("i2c1scl", "i2c1sda", "i2c3scl", "i2c3sda", "spi1nss"):
        assert reason(terminal, f"gpio.cfg {alias} in") == "ok", alias
        assert reason(terminal, f"gpio.release {pins[alias]}") == "ok"


# spi extensions (D.4)


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=8", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=12", "unsupported"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=12 dma=1", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=4 dma=1", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=16 dma=1", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=16 sync=1", "unsupported"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=3 dma=1", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=17 dma=1", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 bits=wide", "usage"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 lsb=1", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 lsb=2", "range"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PA4", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=spi1nss", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PA15", "ok"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PA4 cs=PB0", "usage"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PB12", "pin"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PB12 bits=12", "pin"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PA16", "pin"),
        ("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PA5", "pin"),
        ("spi.open 2 clk=spi2clk mosi=spi2mosi miso=spi2miso nss=spi2nss bits=16 dma=1 lsb=1", "ok"),
    ],
)
def test_spi_open_extensions_wb55(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("spi.open 3 clk=PA0 mosi=PB8 miso=PB9 bits=16 dma=1", "ok"),
        ("spi.open 3 clk=PA0 mosi=PB8 miso=PB9 bits=8 dma=1", "ok"),
        ("spi.open 3 clk=PA0 mosi=PB8 miso=PB9 bits=12 dma=1", "unsupported"),
        ("spi.open 3 clk=PA0 mosi=PB8 miso=PB9 bits=4 dma=1", "unsupported"),
        ("spi.open 3 clk=spi3clk mosi=spi3mosi miso=spi3miso nss=spi3nss", "ok"),
        ("spi.open 3 clk=PA0 mosi=PB8 miso=PB9 nss=PA12", "pin"),
        ("spi.open 1 clk=PB4 mosi=PA15 miso=PB3 bits=12 dma=1", "ok"),
        ("spi.open 1 clk=PB4 mosi=PA15 miso=PB3 nss=PA12 lsb=1", "ok"),
    ],
)
def test_spi_open_extensions_wba55(line, expected):
    terminal, _ = wba()
    assert reason(terminal, line) == expected


def test_spi_holds_its_peripheral():
    terminal, firmware = make_terminal()
    terminal.command("spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PA4 bits=16 dma=1 lsb=1")
    state = firmware.opened[("spi", "1")]
    assert (state["bits"], state["lsb"], state["nss"]) == (16, True, "PA4")
    assert firmware.resources == {("spi", 1): ("spi", "1")}
    assert reason(terminal, "gpio.cfg spi1nss in") == "busy"
    terminal.command("spi.close 1")
    assert firmware.resources == {}
    terminal.command("gpio.cfg PA15 in")
    assert reason(terminal, "spi.open 1 clk=PA5 mosi=PA7 miso=PA6 nss=PA15") == "busy"


# pwm extensions (D.10)


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("pwm.open 1 channels=1 mode=edgedown", "ok"),
        ("pwm.open 1 channels=1 mode=centerup freq=1000", "ok"),
        ("pwm.open 1 channels=1 mode=centerboth freq=1000", "ok"),
        ("pwm.open 1 channels=1 mode=centerup freq=488", "range"),
        ("pwm.open 1 channels=1 mode=edgedown freq=976", "range"),
        ("pwm.open 1 channels=1 mode=diagonal", "usage"),
        ("pwm.open 16 channels=1 mode=edgedown", "unsupported"),
        ("pwm.open 17 channels=1 mode=centerup", "unsupported"),
        ("pwm.open 16 channels=1 mode=centerboth", "unsupported"),
        ("pwm.open 1 channels=1 preload=0", "ok"),
        ("pwm.open 1 channels=1 preload=2", "range"),
        ("pwm.open 1 channels=1 brkfilter=3", "usage"),
        ("pwm.open 1 channels=1 brkfilter=16", "range"),
        ("pwm.open 1 channels=1 brk=PB12 brkfilter=15", "ok"),
        ("pwm.open 1 channels=1 brk=PB12 brkfilter=0", "ok"),
        ("pwm.open 1 channels=1 brk=PB12 brkfilter=16", "range"),
        ("pwm.open 2 channels=1 brk=PB12 brkfilter=2", "unsupported"),
        ("pwm.open 1 channels=1 trgo=update", "ok"),
        ("pwm.open 1 channels=1 trgo=oc4ref", "ok"),
        ("pwm.open 2 channels=1 trgo=oc1", "ok"),
        ("pwm.open 1 channels=1 trgo=never", "usage"),
        ("pwm.open 16 channels=1 trgo=update", "unsupported"),
        ("pwm.open 17 channels=1 trgo=reset", "unsupported"),
    ],
)
def test_pwm_open_extensions(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


def test_pwm_open_extensions_keep_their_settings():
    terminal, firmware = make_terminal()
    terminal.command("pwm.open 1 channels=1 mode=centerup preload=0 trgo=update brk=PB12 brkfilter=4 freq=1000")
    state = firmware.opened[("pwm", "1")]
    assert (state["mode"], state["alignment"], state["preload"], state["trgo"], state["brkfilter"]) == (
        "centerup",
        "center",
        False,
        "update",
        4,
    )
    assert reason(terminal, "pwm.freq 1 488") == "range", "centre aligned: 2 * ARR ticks per period"


# adc trgo (D.10)


def test_adc_trgo_runs_on_a_pwm_timer():
    terminal, firmware, clock = timed_terminal()
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=2") == "unsupported", "nobody drives TIM2"
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=3") == "range"
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=16") == "unsupported"
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=2 timer=2") == "usage"
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=2 rate=100") == "usage"
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=x") == "usage"
    terminal.command("pwm.open 2 channels=1 freq=100 trgo=update")
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=2") == "ok"
    assert firmware.timer_owners[2] == ("pwm", "2"), "the adc group does not take the timer"
    firmware.adc_codes["PC3"] = 7
    start = clock.now
    assert reason(terminal, "adc.measure 1 n=10") == "timeout", "the timer does not run before pwm.duty"
    assert clock.now - start == pytest.approx(1.0)
    terminal.command("pwm.duty 2 50")
    start = clock.now
    assert terminal.command("adc.measure 1 n=10").as_ints("samples") == [7] * 10
    reply = "OK samples=" + ",".join(["7"] * 10)
    assert clock.now - start == pytest.approx(10 / 100 + expect.terminal_line_time(reply, 921600))


def test_adc_trgo_on_a_timer_another_group_holds_is_busy():
    terminal, _ = make_terminal()
    terminal.command("qei.open 1 a=PA8 b=PA9")
    assert reason(terminal, "adc.open 1 pins=PC3 trgo=1") == "busy"
    assert reason(terminal, "adc.open 1 pins=PB0 trgo=1") == "pin", "argument errors come first"


def test_adc_trgo_cannot_use_tim1_on_wba55():
    terminal, _ = wba()
    terminal.command("pwm.open 1 channels=1 trgo=update")
    assert reason(terminal, "adc.open 4 pins=PA7 trgo=1") == "unsupported", "ADC4 reaches TIM1 through TRGO2 only"
    terminal.command("pwm.close 1")
    terminal.command("pwm.open 2 channels=1 trgo=update")
    assert reason(terminal, "adc.open 4 pins=PA7 trgo=2") == "ok"


def test_adc_holds_its_peripheral_and_dma_channel():
    terminal, firmware = wba()
    terminal.command("adc.open 4 pins=PA7")
    assert firmware.resources == {("adc", 4): ("adc", "4"), ("dma1", 7): ("adc", "4")}
    terminal.command("adc.close 4")
    assert firmware.resources == {}


# uart send-only (D.13)


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("uart.open 1 lp=1 sendonly=1", "ok"),
        ("uart.open 1 lp=1 tx=PA2 sendonly=1", "ok"),
        ("uart.open 1 lp=1 tx=PA2", "usage"),
        ("uart.open 1 lp=1 rx=PA3 sendonly=1", "usage"),
        ("uart.open 1 lp=1 sendonly=2", "range"),
        ("uart.open 1 lp=1 sendonly=1 dma=1", "usage"),
        ("uart.open 1 lp=1 sendonly=1 sync=1", "usage"),
        ("uart.open 1 lp=1 sendonly=1 duplex=1", "usage"),
        ("uart.open 1 lp=1 tx=PA2 sendonly=1 flow=rts rts=PB12", "ok"),
        ("uart.open 1 lp=1 tx=PA2 sendonly=1 flow=cts cts=PA6", "unsupported"),
        ("uart.open 1 lp=1 tx=PA2 sendonly=1 flow=rtscts rts=PB12 cts=PA6", "unsupported"),
        ("uart.open 1 lp=1 tx=PA2 sendonly=1 parity=even", "unsupported"),
        ("uart.open 1 lp=1 tx=PA2 sendonly=1 swap=1", "unsupported"),
        ("uart.open 1 lp=1 tx=PA3 sendonly=1", "pin"),
        ("uart.open 1 sendonly=1", "busy"),
    ],
)
def test_uart_sendonly_wb55(line, expected):
    terminal, _ = make_terminal()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("uart.open 2 tx=PB0 sendonly=1", "ok"),
        ("uart.open 2 tx=usart2tx rts=usart2rts flow=rts sendonly=1", "ok"),
        ("uart.open 1 lp=1 tx=PB5 sendonly=1", "unsupported"),
        ("uart.open 2 tx=PA11 sendonly=1", "pin"),
    ],
)
def test_uart_sendonly_wba55(line, expected):
    terminal, _ = wba()
    assert reason(terminal, line) == expected


def test_uart_sendonly_receives_nothing():
    terminal, firmware = make_terminal()
    terminal.command("uart.open 1 lp=1 tx=PA2 sendonly=1")
    assert reason(terminal, "gpio.cfg PA3 in") == "ok", "no RX pin is claimed"
    firmware.uart_rx[1] += b"\x55"
    assert terminal.command("uart.send 1 55").ok
    assert terminal.command("uart.recv 1").raw == "OK data=-"
    assert reason(terminal, "uart.recv 1 len=0") == "range", "the options are still checked"


# encoder on LPTIM (D.9)


def test_lp_encoder_holds_its_lptim():
    terminal, firmware = wba()
    terminal.command("qei.open 2 lp=1 a=lptim2in1 b=lptim2in2 cap=rise")
    assert firmware.resources == {("lpTimer", 2): ("qei", "2")}
    assert firmware.opened[("qei", "2")]["cap"] == "rise"
    assert reason(terminal, "pwm.open 2 channels=1") == "ok", "LPTIM2 is not TIM2"
    terminal.command("qei.close 2")
    assert firmware.resources == {}
