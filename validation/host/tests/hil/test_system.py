"""General commands, framing, pins and error semantics (no AD3 needed).

Scenarios: features/system.feature.
"""

import re
import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.protocol import is_alias


def reason_of(fw, line):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    return error.value.reason


@scenario("system.feature", "The board answers ping")
def test_ping():
    pass


@scenario("system.feature", "The board info matches the board file")
def test_info_matches_board():
    pass


@scenario("system.feature", "The firmware and the board file have the same pin aliases")
def test_board_pins_match_yaml():
    pass


@scenario("system.feature", "The board file uses generic aliases, including the terminal pins")
def test_aliases_are_generic():
    pass


@scenario("system.feature", "Every alias names its pin in commands")
def test_every_alias_is_accepted_as_pin():
    pass


@pytest.mark.board_params("pin", "system.reserved_pins")
@scenario("system.feature", "The reserved pins are busy")
def test_reserved_pins_are_busy(pin):
    pass


@scenario("system.feature", "The terminal pins and the debug LED are busy")
def test_terminal_pins_and_debug_led_are_busy():
    pass


@pytest.mark.board_params("pin", "system.unbonded_pins")
@scenario("system.feature", "A pin the package does not bond out is refused")
def test_unbonded_pin(pin):
    pass


@pytest.mark.board_params("pin", "system.invalid_pins")
@scenario("system.feature", "A malformed pin name is refused")
def test_pin_syntax(pin):
    pass


@pytest.mark.board_params("index", "system.reserved_uarts")
@scenario("system.feature", "The terminal UART is reserved")
def test_terminal_uart_is_reserved(index):
    pass


@scenario("system.feature", "Unknown commands and keys are refused")
def test_unknown_command_and_key():
    pass


@pytest.mark.board_params("line", "system.missing_instances")
@scenario("system.feature", "A command on a missing instance is refused")
def test_nonexistent_instance(line):
    pass


@pytest.mark.board_params("line", "system.unsupported_instances")
@scenario("system.feature", "A command on an unsupported instance is refused")
def test_unsupported_instance(line):
    pass


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
@scenario("system.feature", "Malformed commands are refused")
def test_usage_errors(name, args, reason):
    pass


@scenario("system.feature", "Closing what is not open and opening what is open are refused")
def test_notopen_and_busy():
    pass


@pytest.mark.board_params("ms", "system.delays_ms")
@scenario("system.feature", "A delay lasts at least the requested time")
def test_delay(ms):
    pass


@scenario("system.feature", "A reset reports a software reset")
def test_reset_reports_boot():
    pass


@given("the first of the SPI instances of the board file", target_fixture="spi_instance")
def first_spi_instance(board_cfg):
    return board_cfg.param("spi.instances")[0]


@when("the board info is read", target_fixture="info")
def read_info(fw):
    return fw.system.info()


@when("the firmware lists its pin aliases", target_fixture="reported")
def list_pins(fw):
    return fw.system.pins()


@when("it is opened on its CLK, MOSI and MISO pins")
def spi_opened(fw, spi_instance):
    pins = {key: spi_instance[key] for key in ("clk", "mosi", "miso")}
    fw.spi.open(spi_instance["index"], **pins)


@when("the firmware delays for the time, timed on the host", target_fixture="elapsed")
def timed_delay(fw, ms):
    start = time.monotonic()
    fw.system.delay(ms)
    return time.monotonic() - start


@when("the board resets", target_fixture="boot")
def reset_board(fw, board_cfg):
    return fw.system.reset(timeout=board_cfg.param("system.boot_timeout", 5.0))


@then("the board answers ping")
def answers_ping(fw):
    fw.system.ping()


@then("it names the board of the board file")
def info_names_board(board_cfg, info):
    assert board_cfg.matches_firmware_name(info.board), info.raw


@then("it reports the family of the board file")
def info_reports_family(board_cfg, info):
    assert info.family == board_cfg.family


@then("it reports the system clock of the board file, which is the sysclk of its clock tree")
def info_reports_sysclk(board_cfg, info):
    assert info.sysclk == board_cfg.sysclk == board_cfg.clock("sysclk")


@then(parsers.parse("it reports one of the reset causes {causes}"))
def info_reports_reset_cause(info, causes):
    first, last = causes.split(" and ")
    assert info.reset in (*first.split(", "), last)


@then(parsers.parse("it reports a unique device ID of {digits:d} hex digits"))
def info_reports_uid(info, digits):
    assert info.uid is not None and re.fullmatch(rf"[0-9a-fA-F]{{{digits}}}", info.uid), "the 96-bit unique device ID"


@then("every alias of the board file names the same pin in the firmware")
def aliases_match(board_cfg, reported):
    mismatches = {alias: (pin, reported.get(alias)) for alias, pin in board_cfg.pins.items() if reported.get(alias) != pin}
    assert not mismatches, f"alias: (yaml, firmware) {mismatches}"


@then("the firmware has no alias the board file lacks")
def no_extra_aliases(board_cfg, reported):
    extra = {alias: pin for alias, pin in reported.items() if alias not in board_cfg.pins}
    assert not extra, f"aliases the board file lacks: {extra}"


@then("every alias of the board file is a generic alias")
def aliases_generic(board_cfg):
    assert all(is_alias(alias) for alias in board_cfg.pins), sorted(board_cfg.pins)


@then(parsers.parse("the board file has the aliases {first} and {second}"))
def has_terminal_aliases(board_cfg, first, second):
    assert {first, second} <= set(board_cfg.pins)


@then(
    parsers.parse(
        "every alias of the board file configures as a GPIO input, its pin reads and the alias releases, "
        'except that configuring an alias of a terminal pin or of the debug LED fails with "{refusal}"'
    )
)
def aliases_accepted(fw, board_cfg, refusal):
    reserved = {*board_cfg.terminal.pins, board_cfg.resolve_pin(board_cfg.param("system.debug_led"))}
    for alias, pin in board_cfg.pins.items():
        if pin in reserved:
            assert reason_of(fw, f"gpio.cfg {alias} in") == refusal, alias
            continue
        fw.command("gpio.cfg", alias, "in")
        fw.command("gpio.get", pin)
        fw.command("gpio.release", alias)


@then(parsers.parse('configuring the pin as a GPIO input fails with "{refusal}"'))
def pin_refused(fw, pin, refusal):
    assert reason_of(fw, f"gpio.cfg {pin} in") == refusal


@then(parsers.parse('configuring each terminal pin and then the debug LED pin as a GPIO input fails with "{refusal}"'))
def terminal_pins_and_debug_led_refused(fw, board_cfg, refusal):
    for pin in [*board_cfg.terminal.pins, board_cfg.param("system.debug_led")]:
        assert reason_of(fw, f"gpio.cfg {pin} in") == refusal, pin


@then(parsers.parse('opening the reserved UART fails with "{refusal}"'))
def terminal_uart_refused(fw, index, refusal):
    with pytest.raises(FirmwareError) as error:
        fw.uart.open(index)
    assert error.value.reason == refusal


@then(parsers.parse('the command line "{command_line}" fails with "{refusal}"'))
def unknown_command_refused(fw, command_line, refusal):
    assert reason_of(fw, command_line) == refusal


@then(parsers.parse('the command "{command}" with {key}={value:d} fails with "{refusal}"'))
def unknown_key_refused(fw, command, key, value, refusal):
    with pytest.raises(FirmwareError) as error:
        fw.command(command, **{key: value})
    assert error.value.reason == refusal


@then(parsers.parse('the command line fails with "{refusal}"'))
def line_refused(fw, line, refusal):
    assert reason_of(fw, line) == refusal


@then("the command with the arguments fails with the reason")
def usage_refused(fw, name, args, reason):
    assert reason_of(fw, " ".join((name, *args))) == reason


@then(parsers.parse('closing it fails with "{refusal}"'))
def spi_close_refused(fw, spi_instance, refusal):
    with pytest.raises(FirmwareError) as error:
        fw.spi.close(spi_instance["index"])
    assert error.value.reason == refusal


@then(parsers.parse('opening it on these pins again fails with "{refusal}"'))
def spi_reopen_refused(fw, spi_instance, refusal):
    pins = {key: spi_instance[key] for key in ("clk", "mosi", "miso")}
    with pytest.raises(FirmwareError) as error:
        fw.spi.open(spi_instance["index"], **pins)
    assert error.value.reason == refusal


@then(parsers.parse('opening it on these pins again with baud={spi_baud:d} fails with "{refusal}", argument errors coming before busy'))
def spi_reopen_bad_baud_refused(fw, spi_instance, spi_baud, refusal):
    pins = {key: spi_instance[key] for key in ("clk", "mosi", "miso")}
    with pytest.raises(FirmwareError) as error:
        fw.spi.open(spi_instance["index"], **pins, baud=spi_baud)
    assert error.value.reason == refusal, "argument errors come before busy"


@then("it closes")
def spi_closes(fw, spi_instance):
    fw.spi.close(spi_instance["index"])


@then(parsers.parse("it took at least {percent:d} % of the time"))
def delay_long_enough(ms, elapsed, percent):
    assert elapsed >= ms / 1000 * (percent / 100)


@then(parsers.parse('the boot message names the board, the family and the system clock of the board file and the reset cause "{cause}"'))
def boot_reports_reset(board_cfg, boot, cause):
    assert board_cfg.matches_firmware_name(boot.board)
    assert boot.family == board_cfg.family
    assert boot.sysclk == board_cfg.sysclk
    assert boot.reset == cause


@then(parsers.parse('the board info reports the reset cause "{cause}"'))
def info_reports_reset(fw, cause):
    assert fw.system.info().reset == cause
