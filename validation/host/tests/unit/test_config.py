import pytest

from hal_st_validation import expect
from hal_st_validation.config import ConfigError, available_boards, load_board, parse_board
from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS
from hal_st_validation.pairwise import pairwise
from hal_st_validation.protocol import is_alias

BOARDS = ["nucleo_wb55rg", "nucleo_wba55cg"]
FIRMWARE_TABLES = {"nucleo_wb55rg": WB55_PINS, "nucleo_wba55cg": WBA55_PINS}
FAMILIES = {"nucleo_wb55rg": "stm32wb55", "nucleo_wba55cg": "stm32wba55"}
CLOCKS = {"nucleo_wb55rg": 64_000_000, "nucleo_wba55cg": 100_000_000}


def test_available_boards():
    assert set(BOARDS) <= set(available_boards())


@pytest.mark.parametrize("name", BOARDS)
def test_board_files_load(name):
    board = load_board(name)
    assert board.name == name
    assert board.family == FAMILIES[name]
    assert board.sysclk == CLOCKS[name]
    assert board.terminal.baud == 921600
    assert board.terminal.uart == 1
    assert board.ad3.vplus is None
    assert board.ad3.analog_max == 3.3
    assert board.matches_firmware_name(name.upper().replace("_", "-"))
    assert board.matches_firmware_name(board.firmware_name)
    assert all(is_alias(alias) for alias in board.pins)
    assert board.terminal.pins == (board.pins["terminaltx"], board.pins["terminalrx"])
    for alias in ("tim1ch1", "tim1bkin", "spi1clk", "lpuart1tx", "led0", "gpio0", "sw1"):
        assert board.resolve_pin(alias).startswith("P")


@pytest.mark.parametrize("name", BOARDS)
def test_alias_table_matches_protocol(name):
    """The board file's `pins` is the PROTOCOL.md table the fake firmware also serves, in the same order."""
    board = load_board(name)
    assert board.pins == FIRMWARE_TABLES[name]
    assert list(board.pins) == list(FIRMWARE_TABLES[name])


@pytest.mark.parametrize("name", BOARDS)
def test_clocks(name):
    board = load_board(name)
    hz = CLOCKS[name]
    assert board.clock("sysclk") == board.clock("pclk1") == board.clock("timer") == hz
    assert board.clock("spi", 1) == hz
    assert board.clock("uart", "lpuart1") == board.clock("uart", "LPUART1") == hz
    assert board.clock("uart", expect.uart_clock_name(1, lp=True)) == hz
    with pytest.raises(ConfigError):
        board.clock("spi", 2 if name == "nucleo_wba55cg" else 3)
    with pytest.raises(ConfigError):
        board.clock("nosuchclock")


def test_clock_tables_follow_the_instances():
    assert set(load_board("nucleo_wb55rg").clocks["spi"]) == {1, 2}
    assert set(load_board("nucleo_wba55cg").clocks["spi"]) == {1, 3}
    assert set(load_board("nucleo_wba55cg").clocks["uart"]) == {"usart1", "usart2", "lpuart1"}


@pytest.mark.parametrize("name", BOARDS)
def test_bundles_wire_each_dio_once(name):
    """Each bundle uses an AD3 channel at most once, each DIO on its own pin, and all 16 DIOs; no bundle wires
    a reserved pin."""
    board = load_board(name)
    reserved = set(board.terminal.pins) | {board.resolve_pin(board.param("system.debug_led"))}
    reserved |= {board.resolve_pin(pin) for pin in board.param("system.reserved_pins")}
    for bundle in board.wiring_sets:
        connections = board.wiring([bundle]).connections
        dios = [connection for connection in connections if connection.kind == "dio"]
        assert len({connection.channel for connection in dios}) == len(dios) == 16
        assert len({connection.pin for connection in dios}) == len(dios)
        assert not {pin for connection in connections for pin in connection.pins} & reserved


def test_bundle_sets():
    assert set(load_board("nucleo_wb55rg").wiring_sets) == {"bundle1", "bundle2"}
    assert set(load_board("nucleo_wba55cg").wiring_sets) == {"bundle1", "bundle2"}


@pytest.mark.parametrize(
    ("name", "offered"),
    [
        ("nucleo_wb55rg", {"bundle1": {"loopback", "i2c", "spiloop"}, "bundle2": {"loopback"}}),
        ("nucleo_wba55cg", {"bundle1": {"loopback"}, "bundle2": {"loopback", "i2c", "spiloop"}}),
    ],
)
def test_bundle_options(name, offered):
    """The options each set offers; loopback and spiloop share pins, so they exclude each other."""
    board = load_board(name)
    assert {bundle: set(wiring_set.options) for bundle, wiring_set in board.wiring_sets.items()} == offered
    for bundle, tags in offered.items():
        if {"loopback", "spiloop"} <= tags:
            with pytest.raises(ConfigError):
                board.wiring([bundle], ["loopback", "spiloop"])
        for tag in tags:
            option = board.wiring([bundle], [tag]).options[tag]
            assert option.loads and set(option.jumpered) <= set(option.loads), (bundle, tag)
            assert set(option.pullups) <= set(option.loads), (bundle, tag)


def test_wba55_bundle2_moves_four_dios():
    board = load_board("nucleo_wba55cg")
    bundle1, bundle2 = board.wiring(["bundle1"]), board.wiring(["bundle2"])
    assert [bundle1.dio(pin) for pin in ("PB5", "PA10", "PB15", "PA2")] == [8, 9, 11, 14]
    assert [bundle2.dio(pin) for pin in ("PA7", "PA6", "PB8", "PA0")] == [8, 9, 11, 14]
    assert bundle2.wavegen("PA7") is None and bundle2.scope("PB2") is None
    with_i2c = board.wiring(["bundle2"], ["i2c"])
    assert with_i2c.scope("PB2") == 1 and with_i2c.scope("PA6", allowed=["i2c"]) == 1
    assert with_i2c.loaded("PA6") == ("i2c",) and with_i2c.scope("PA6") is None
    with pytest.raises(ConfigError):
        board.wiring(["bundle1", "bundle2"])


def test_wb55_bundle2_moves_the_encoder_inputs():
    board = load_board("nucleo_wb55rg")
    bundle1, bundle2 = board.wiring(["bundle1"]), board.wiring(["bundle2"])
    assert bundle1.dio("PA15") == 9 and bundle1.dio("PA1") == 10
    assert bundle2.dio("PC0") == 9 and bundle2.dio("PC2") == 10
    assert bundle2.wavegen("PC2") is None and bundle2.scope("PC2") is None
    assert bundle1.wavegen("PC2") == 2
    with pytest.raises(ConfigError):
        board.wiring(["bundle1", "bundle2"])


@pytest.mark.parametrize("name", BOARDS)
def test_parameters_reference_wired_pins(name):
    """Pins in the test parameters are wired in bundle1 (the LPTIM encoder in bundle2); PWM timers and SPI instances are
    wired in some set, SPI instances through the jumpers of their `option` when they name one."""
    board = load_board(name)

    def wired(pin, kind="dio", bundle="bundle1"):
        return board.wiring([bundle]).channel(kind, board.resolve_pin(pin)) is not None

    def wired_in_a_set(pin, option=None):
        tags = [option] if option else []
        sets = [set_name for set_name, wiring_set in board.wiring_sets.items() if option is None or option in wiring_set.options]
        return any(board.wiring([bundle], tags).channel("dio", board.resolve_pin(pin), allowed=tags) is not None for bundle in sets)

    for pin in board.param("gpio.loop_pins") + board.param("gpio.output_pins"):
        assert wired(pin), pin
    sharing = board.param("gpio.exti_sharing")
    counting, other = board.resolve_pin(sharing["counting"]), board.resolve_pin(sharing["sharing"])
    assert wired(counting) and wired(other)
    assert counting[2:] == other[2:] and counting[1] != other[1], "same EXTI line, other port"
    for timer in board.param("pwm.timers"):
        for channel in timer["channels"]:
            assert wired_in_a_set(channel["pin"]), channel
            assert channel.get("npin") is None or wired_in_a_set(channel["npin"]), channel
        assert timer.get("brk") is None or wired_in_a_set(timer["brk"]), timer
    for instance in board.param("uart.instances"):
        assert all(wired(instance[key]) for key in ("tx", "rx", "rts", "cts")), instance
    for instance in board.param("spi.instances"):
        assert all(wired_in_a_set(instance[key], instance.get("option")) for key in ("clk", "cs", "mosi", "miso")), instance
    for instance in board.param("qei.instances"):
        assert all(wired(instance[key]) for key in ("a", "b", "idx")), instance
    for instance in board.param("qei.lp_instances"):
        assert all(wired(instance[key], bundle="bundle2") for key in ("a", "b")), instance
    assert wired(board.param("watchdog.pin"))
    for pin in board.param("adc.inputs"):
        assert wired(pin, "wavegen") and wired(pin, "scope"), pin


@pytest.mark.parametrize("name", BOARDS)
def test_parameters_use_the_pin_functions(name):
    """The pins the tests open for a function carry it in the generated pinout tables (as the fake models them)."""
    from hal_st_validation.fake_firmware import FakeFirmware

    board = load_board(name)
    fake = FakeFirmware(family=board.family)
    pin = board.resolve_pin
    for timer in board.param("pwm.timers"):
        number = timer["timer"]
        for channel in timer["channels"]:
            assert fake.supports(f"timerChannel{channel['channel']}", number, pin(channel["pin"])), channel
            if channel.get("npin"):
                assert fake.supports(f"timerChannel{channel['channel']}N", number, pin(channel["npin"])), channel
        if timer.get("brk"):
            assert fake.supports("timerBreak", number, pin(timer["brk"])), timer
    for instance in board.param("uart.instances"):
        prefix = "lpuart" if instance["lp"] else "uart"
        for key in ("tx", "rx", "rts", "cts"):
            assert fake.supports(prefix + key.capitalize(), instance["index"], pin(instance[key])), (instance, key)
    for instance in board.param("spi.instances"):
        for key, function in (("clk", "spiClock"), ("mosi", "spiMosi"), ("miso", "spiMiso")):
            assert fake.supports(function, instance["index"], pin(instance[key])), (instance, key)
    for instance in board.param("qei.instances"):
        assert fake.supports("timerChannel1", instance["index"], pin(instance["a"]))
        assert fake.supports("timerChannel2", instance["index"], pin(instance["b"]))
    for instance in board.param("qei.lp_instances"):
        assert fake.supports("lpTimerInput1", instance["index"], pin(instance["a"]))
        assert fake.supports("lpTimerInput2", instance["index"], pin(instance["b"]))
    for alias in board.param("adc.inputs") + board.param("adc.spare_inputs"):
        assert pin(alias) in fake.spec.analog, alias


@pytest.mark.parametrize("name", BOARDS)
def test_matrices_load(name):
    board = load_board(name)
    for path in (
        "gpio.irq",
        "pwm.waveform",
        "pwm.channels",
        "pwm.complementary",
        "pwm.idle",
        "pwm.break",
        "pwm.limits",
        "pwm.frequency_change",
    ):
        assert board.matrix(path)
    for path in (
        "uart.transfer",
        "uart.flow",
        "uart.swap",
        "uart.large",
        "uart.stall",
        "spi.transfer",
        "spi.sessions",
        "spi.receive_only",
        "spi.largest",
    ):
        assert board.matrix(path)
    for path in (
        "adc.levels",
        "adc.sequence",
        "adc.timing",
        "adc.trigger_rate",
        "qei.position",
        "qei.lp_position",
        "qei.velocity",
        "qei.rollover",
    ):
        assert board.matrix(path)
    for path in ("watchdog.behaviour", "watchdog.period"):
        assert board.matrix(path)
    assert board.matrix("gpio.irq")["handler"] == ["immediate", "dispatched"]
    assert board.param("gpio.drives") == ["low", "medium", "fast", "high"]
    assert board.matrix("adc.timing")["sampling"] == list(expect.ADC_SAMPLING_TIMES[board.family])
    assert set(board.matrix("uart.transfer")["variant"]) == set(board.param("uart.variants"))
    assert len(pairwise(board.matrix("pwm.waveform"))) < 100
    with pytest.raises(ConfigError):
        board.matrix("pwm.timers")


@pytest.mark.parametrize("name", BOARDS)
def test_unsupported_commands_cover_the_protocol(name):
    """`tests.unsupported.commands` names every command PROTOCOL.md lists as unavailable on the board's MCU."""
    from hal_st_validation.fake_firmware import UNSUPPORTED_COMMANDS

    board = load_board(name)
    assert {line.split()[0] for line in board.param("unsupported.commands")} == set(UNSUPPORTED_COMMANDS[board.family])


@pytest.mark.parametrize("name", BOARDS)
def test_gpio_limit_pins(name):
    board = load_board(name)
    pins = [board.resolve_pin(pin) for pin in board.param("gpio.limit_pins")]
    assert len(set(pins)) == len(pins) == board.param("gpio.limit") + 1
    reserved = set(board.terminal.pins) | {board.resolve_pin(pin) for pin in board.param("system.reserved_pins")}
    assert not set(pins) & reserved


@pytest.mark.parametrize("name", BOARDS)
def test_qei_resolution_limits(name):
    """TIM2 counts up to 2^32 - 1 (EMIL's number parser), the 16-bit timers and LPTIM1 up to 65536."""
    board = load_board(name)
    for instance in board.param("qei.instances") + board.param("qei.lp_instances"):
        assert instance["max_res"] == (0xFFFFFFFF if instance["index"] in expect.TIMERS_32BIT and "lp" not in instance["name"] else 65536)


@pytest.mark.parametrize("name", BOARDS)
def test_uart_instance_variants(name):
    board = load_board(name)
    variants = board.param("uart.variants")
    for instance in board.param("uart.instances"):
        assert set(instance["variants"]) <= set(variants)
        if instance["lp"]:
            assert not {"duplex", "sync"} & set(instance["variants"]), "LPUART takes neither duplex nor sync"


@pytest.mark.parametrize("name", BOARDS)
def test_numeric_parameters_match_the_clocks(name):
    """The parameter lists straddle the limits expect.py derives from the board clocks."""
    board = load_board(name)
    spiclk = board.clock("spi", 1)
    assert [expect.spi_baud_fits(spiclk, baud) for baud in board.param("spi.open_bauds")] == [False, True, True, False]
    assert all(expect.spi_baud_fits(spiclk, baud) for baud in board.matrix("spi.transfer")["baud"])
    assert expect.spi_baud_fits(spiclk, board.param("spi.session_baud"))
    timeouts = board.param("watchdog.open_timeouts_ms")
    fits = [expect.wwdg_prescaler_for(timeout, board.clock("pclk1")) is not None for timeout in timeouts if timeout <= 30000]
    assert fits == [True, True, False, False]
    for timeout in board.matrix("watchdog.behaviour")["timeout_ms"]:
        assert expect.wwdg_prescaler_for(timeout, board.clock("pclk1")) is not None
    maximum = expect.uart_baud_max(board.family)
    for instance in board.param("uart.instances"):
        lp = bool(instance["lp"])
        clock = board.clock("uart", expect.uart_clock_name(instance["index"], lp))
        bauds = board.matrix("uart.transfer")["baud"] + board.matrix("uart.large")["baud"] + [board.param("uart.flow_baud")]
        bauds += [baud for baud, _ in board.param("uart.reopen_settings")]
        for baud in bauds:
            assert expect.uart_baud_fits(clock, baud, lp, maximum), (instance["name"], baud)
        results = {baud: expect.uart_baud_fits(clock, baud, lp, maximum) for baud in board.param("uart.open_bauds")}
        assert True in results.values() and False in results.values()
        low, high = expect.uart_baud_limits(clock, lp, maximum)
        assert {low - 1, low, high, high + 1} <= set(board.param("uart.open_bauds")) | {299, 12000001}, instance["name"]
    pwm_clock = board.clock("timer")
    for freq in board.param("pwm.frequency_changes") + [board.param("pwm.channels_frequency"), board.param("pwm.complementary_frequency")]:
        for timer in board.param("pwm.timers"):
            assert expect.pwm_fits(pwm_clock, freq, "center", expect.timer_counter_max(timer["timer"])), (timer["name"], freq)
    assert expect.wwdg_prescaler_for(board.param("watchdog.manual_timeout_ms"), board.clock("pclk1")) is not None


def test_wiring_lookup_and_conflicts():
    board = load_board("nucleo_wb55rg")
    wiring = board.wiring(["bundle1"], ["loopback"])
    assert wiring.dio(board.resolve_pin("tim1ch1")) == 0
    assert wiring.dio(board.resolve_pin("spi1mosi")) == 1, "PA7 is TIM1 CH1N and SPI1 MOSI"
    assert wiring.wavegen(board.resolve_pin("ain4")) == 1
    assert wiring.scope(board.resolve_pin("ain3")) == 2
    assert wiring.has("loopback")
    assert "DIO15" in wiring.describe()
    with pytest.raises(ConfigError):
        board.wiring(["nosuchset"])
    other = load_board("nucleo_wba55cg")
    assert other.wiring(["bundle1"]).dio("PB4") == 0
    assert other.wiring(["bundle1"]).wavegen("PA7") == 1


def test_optional_connections_and_roles():
    raw = {
        "board": "x",
        "family": "stm32wb55",
        "pins": {"gpio0": "PC6"},
        "wiring_sets": {
            "s": {"options": {"extra": "a probe"}, "dio": {0: "gpio0", 1: {"pin": "PB0", "role": "probe", "requires": "extra"}}}
        },
    }
    board = parse_board(raw)
    assert board.wiring(["s"]).dio(role="probe") is None
    assert board.wiring(["s"], ["extra"]).dio(role="probe") == 1
    raw["wiring_sets"]["j"] = {"wavegen": {1: {"pin": "PC3", "jumpered": ["PC2"]}}}
    assert parse_board(raw).wiring(["j"]).wavegen("PC2") == 1
    raw["wiring_sets"]["j"] = {"wavegen": {1: {"pin": "PC3", "jumpered": "PC2"}}}
    with pytest.raises(ConfigError):
        parse_board(raw)


def test_params_and_overrides():
    board = load_board("nucleo_wb55rg")
    assert board.param("watchdog.index") == 0
    assert board.param("pwm.missing", 5) == 5
    with pytest.raises(ConfigError):
        board.param("pwm.missing")
    board.apply_overrides(["pwm.waveform.freq=[20000]", "new.value=1.5"])
    assert board.matrix("pwm.waveform")["freq"] == [20000]
    assert board.param("new.value") == 1.5
    with pytest.raises(ConfigError):
        board.apply_overrides(["novalue"])
    assert board.aliases_of("PA8")[:2] == ["tim1ch1", "qei1a"], "in table order (PA8 is also mco)"
    assert load_board("nucleo_wb55rg").matrix("pwm.waveform")["freq"] != [20000], "overrides stay in one BoardConfig"


def test_invalid_boards_rejected():
    raw = {"board": "x", "family": "stm32wb55", "wiring_sets": {"bad": {"dio": {16: "PA0"}}}}
    with pytest.raises(ConfigError):
        parse_board(raw)
    raw = {"board": "x", "family": "stm32wb55", "wiring_sets": {"bad": {"wavegen": {3: "PA0"}}}}
    with pytest.raises(ConfigError):
        parse_board(raw)
    with pytest.raises(ConfigError, match="generic"):
        parse_board({"board": "x", "family": "stm32wb55", "pins": {"phasea": "PC3"}})
    with pytest.raises(ConfigError):
        parse_board({"board": "x", "family": "stm32wb55", "pins": {"gpio0": "PA16"}})
    with pytest.raises(ConfigError):
        parse_board({"board": "x", "family": "stm32wb55", "pins": {"ain1": "PC0"}, "wiring_sets": {"s": {"dio": {0: "ain2"}}}})
    with pytest.raises(ConfigError):
        parse_board({"board": "x", "family": "stm32wb55", "clocks": {"spi": {1: "fast"}}})
