"""General commands, framing, pins and error semantics (no AD3 needed)."""

import re
import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.protocol import is_alias


def reason_of(fw, line):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    return error.value.reason


def test_ping(fw):
    fw.system.ping()


def test_info_matches_board(fw, board_cfg):
    info = fw.system.info()
    assert board_cfg.matches_firmware_name(info.board), info.raw
    assert info.family == board_cfg.family
    assert info.sysclk == board_cfg.sysclk == board_cfg.clock("sysclk")
    assert info.reset in ("iwdg", "wwdg", "sw", "lpwr", "obl", "bor", "pin", "unknown")
    assert info.uid is not None and re.fullmatch(r"[0-9a-fA-F]{24}", info.uid), "the 96-bit unique device ID"


def test_board_pins_match_yaml(fw, board_cfg):
    """`board.pins` and the board file's `pins` are the same alias table, in both directions."""
    reported = fw.system.pins()
    mismatches = {alias: (pin, reported.get(alias)) for alias, pin in board_cfg.pins.items() if reported.get(alias) != pin}
    assert not mismatches, f"alias: (yaml, firmware) {mismatches}"
    extra = {alias: pin for alias, pin in reported.items() if alias not in board_cfg.pins}
    assert not extra, f"aliases the board file lacks: {extra}"


def test_aliases_are_generic(board_cfg):
    assert all(is_alias(alias) for alias in board_cfg.pins), sorted(board_cfg.pins)
    assert {"terminaltx", "terminalrx"} <= set(board_cfg.pins)


def test_every_alias_is_accepted_as_pin(fw, board_cfg):
    """Each alias names its pin in commands; the terminal pins and the debug LED stay reserved."""
    reserved = {*board_cfg.terminal.pins, board_cfg.resolve_pin(board_cfg.param("system.debug_led"))}
    for alias, pin in board_cfg.pins.items():
        if pin in reserved:
            assert reason_of(fw, f"gpio.cfg {alias} in") == "busy", alias
            continue
        fw.command("gpio.cfg", alias, "in")
        fw.command("gpio.get", pin)
        fw.command("gpio.release", alias)


@pytest.mark.board_params("pin", "system.reserved_pins")
def test_reserved_pins_are_busy(fw, pin):
    """SWD, the LSE crystal and BOOT0 cannot be opened."""
    assert reason_of(fw, f"gpio.cfg {pin} in") == "busy"


def test_terminal_pins_and_debug_led_are_busy(fw, board_cfg):
    for pin in [*board_cfg.terminal.pins, board_cfg.param("system.debug_led")]:
        assert reason_of(fw, f"gpio.cfg {pin} in") == "busy", pin


@pytest.mark.board_params("pin", "system.unbonded_pins")
def test_unbonded_pin(fw, pin):
    """A pin the package does not bond out, or of a port the MCU lacks, is `ERR pin`."""
    assert reason_of(fw, f"gpio.cfg {pin} in") == "pin"


@pytest.mark.board_params("pin", "system.invalid_pins")
def test_pin_syntax(fw, pin):
    """`P<port><index>` without leading zero and index 0-15, or an alias of the board (case-sensitive)."""
    assert reason_of(fw, f"gpio.cfg {pin} in") == "pin"


@pytest.mark.board_params("index", "system.reserved_uarts")
def test_terminal_uart_is_reserved(fw, index):
    with pytest.raises(FirmwareError) as error:
        fw.uart.open(index)
    assert error.value.reason == "busy"


def test_unknown_command_and_key(fw):
    assert reason_of(fw, "no.such.command") == "usage"
    with pytest.raises(FirmwareError) as error:
        fw.command("ping", nosuchkey=1)
    assert error.value.reason == "usage"


@pytest.mark.board_params("line", "system.missing_instances")
def test_nonexistent_instance(fw, line):
    assert reason_of(fw, line) == "range"


@pytest.mark.board_params("line", "system.unsupported_instances")
def test_unsupported_instance(fw, line):
    assert reason_of(fw, line) == "unsupported"


@pytest.mark.parametrize(
    ("name", "args", "reason"),
    [
        ("gpio.set", (), "usage"),
        ("ping", ("extra",), "usage"),
        ("info", ("extra",), "usage"),
        ("delay", (), "usage"),
        ("delay", ("abc",), "usage"),
        ("delay", ("600001",), "range"),
        ("gpio.cfg", ("gpio0", "sideways"), "usage"),
        ("gpio.cfg", ("gpio0", "in", "pull=sideways"), "usage"),
    ],
)
def test_usage_errors(fw, name, args, reason):
    assert reason_of(fw, " ".join((name, *args))) == reason


def test_notopen_and_busy(fw, board_cfg):
    instance = board_cfg.param("spi.instances")[0]
    pins = {key: instance[key] for key in ("clk", "mosi", "miso")}
    with pytest.raises(FirmwareError) as error:
        fw.spi.close(instance["index"])
    assert error.value.reason == "notopen"
    fw.spi.open(instance["index"], **pins)
    with pytest.raises(FirmwareError) as error:
        fw.spi.open(instance["index"], **pins)
    assert error.value.reason == "busy"
    with pytest.raises(FirmwareError) as error:
        fw.spi.open(instance["index"], **pins, baud=1)
    assert error.value.reason == "range", "argument errors come before busy"
    fw.spi.close(instance["index"])


@pytest.mark.board_params("ms", "system.delays_ms")
def test_delay(fw, ms):
    start = time.monotonic()
    fw.system.delay(ms)
    assert time.monotonic() - start >= ms / 1000 * 0.95


@pytest.mark.resets_board
def test_reset_reports_boot(fw, board_cfg):
    boot = fw.system.reset(timeout=board_cfg.param("system.boot_timeout", 5.0))
    assert board_cfg.matches_firmware_name(boot.board)
    assert boot.family == board_cfg.family
    assert boot.sysclk == board_cfg.sysclk
    assert boot.reset == "sw"
    fw.system.ping()
    assert fw.system.info().reset == "sw"
