"""The load gate of `need` (tests/conftest.py), run in pytester sessions with the real conftest and an inline board:
a pin an enabled option loads resolves only for tests marked `uses_option`/`requires_option` of that option, and
a pin an option jumpers to a channel resolves only for them; `--with` is validated against the wiring sets."""

from pathlib import Path

import pytest

pytest_plugins = ["pytester"]

CONFTEST = Path(__file__).resolve().parents[1] / "conftest.py"

BOARD = """
board: gate
family: stm32wb55
pins:
  spi1miso: PA6
wiring_sets:
  one:
    options:
      loopback:
        loads: [PA6, PA7]
        excludes: [spiloop]
      spiloop:
        jumpered: {PA5: [PB13]}
        loads: [PA5, PB13]
        excludes: [loopback]
      i2c:
        jumpered: {PB8: [PC0]}
        loads: [PB8, PC0]
    dio:
      0: PA5
      1: PA7
      3: PB8
      7: spi1miso
    scope:
      1: PA6
  two:
    dio:
      0: PA5
"""

TESTS = """
import pytest


def test_unmarked_loaded(need):
    need.dio("PA6")


def test_unmarked_loaded_by_alias(need):
    need.dio("spi1miso")


@pytest.mark.uses_option("loopback")
def test_marked_loaded(need):
    assert need.dio("PA6") == 7
    assert need.scope("PA6") == 1


def test_unloaded(need):
    assert need.dio("PA5") == 0


@pytest.mark.uses_option("i2c")
def test_jumpered_with_tag(need):
    assert need.dio("PC0") == 3


def test_jumpered_without_tag(need):
    need.dio("PC0")


def test_optional_lookups_of_loaded_pins(need):
    assert need.optional_dio("PA6") is None
    assert need.optional_scope("PA6") is None
    assert need.optional_dio("PA5") == 0


@pytest.mark.requires_option("loopback")
def test_requires_implies_uses(need):
    assert need.dio("PA7") == 1


def test_unloaded_skips_explicitly(need):
    need.unloaded("PA5", "spi1miso")


@pytest.mark.conflicts_option("loopback")
def test_conflicts():
    pass
"""

INI = """
[pytest]
markers =
    uses_option(tag): x
    requires_option(tag): x
    conflicts_option(tag): x
"""


@pytest.fixture
def session(pytester):
    pytester.makeconftest(CONFTEST.read_text(encoding="utf-8"))
    pytester.makeini(INI)
    board = pytester.makefile(".yaml", gate=BOARD)
    pytester.makepyfile(test_gate=TESTS)

    def run(*options):
        return pytester.runpytest_inprocess("--board", str(board), "-p", "no:cacheprovider", "-rs", *options)

    return run


def outcomes(result):
    reports = result.reprec.getreports("pytest_runtest_logreport")
    found = {}
    for report in reports:
        name = report.nodeid.split("::")[-1]
        if report.when == "call" or report.outcome != "passed":
            found[name] = (report.outcome, report.longrepr[2] if report.skipped else "")
    return found


def test_gate_with_options_enabled(session):
    result = session("--wiring-set", "one", "--with", "loopback", "--with", "i2c")
    found = outcomes(result)
    loaded = 'pin PA6 loaded by --with loopback (mark the test uses_option("loopback") if it handles that wiring)'
    assert found["test_unmarked_loaded"] == ("skipped", f"Skipped: {loaded}")
    assert found["test_unmarked_loaded_by_alias"] == ("skipped", f"Skipped: {loaded}")
    assert found["test_marked_loaded"][0] == "passed"
    assert found["test_unloaded"][0] == "passed"
    assert found["test_jumpered_with_tag"][0] == "passed"
    assert found["test_jumpered_without_tag"][0] == "skipped"
    assert "DIO for PC0 is not wired" in found["test_jumpered_without_tag"][1]
    assert found["test_optional_lookups_of_loaded_pins"][0] == "passed"
    assert found["test_requires_implies_uses"][0] == "passed"
    assert found["test_unloaded_skips_explicitly"] == ("skipped", f"Skipped: {loaded}")
    assert found["test_conflicts"] == ("skipped", "Skipped: disconnect --with loopback")


def test_gate_without_options(session):
    result = session("--wiring-set", "one")
    found = outcomes(result)
    assert found["test_unmarked_loaded"][0] == "passed"
    assert found["test_marked_loaded"][0] == "passed"
    assert found["test_jumpered_with_tag"][0] == "skipped", "PC0 is only reached through the i2c jumpers"
    assert found["test_unloaded_skips_explicitly"][0] == "passed"
    assert found["test_requires_implies_uses"] == ("skipped", "Skipped: enable with --with loopback")
    assert found["test_conflicts"][0] == "passed"


@pytest.mark.parametrize(
    ("options", "message"),
    [
        (["--with", "loopback"], "needs the --wiring-set"),
        (["--wiring-set", "two", "--with", "loopback"], "offer no --with loopback"),
        (["--wiring-set", "one", "--with", "loopback", "--with", "spiloop"], "exclude each other"),
        (["--wiring-set", "nosuch"], "unknown wiring set"),
    ],
)
def test_invalid_wiring_is_a_usage_error(session, options, message):
    result = session(*options)
    assert result.ret == pytest.ExitCode.USAGE_ERROR
    assert message in "\n".join(result.errlines)


def test_board_extra_from_the_environment(session, pytester, monkeypatch):
    extra = pytester.makefile(".yaml", extra="wiring_sets:\n  two:\n    options:\n      loopback: {loads: [PA5]}\n")
    monkeypatch.setenv("HAL_ST_BOARD_EXTRA", str(extra))
    pytester.makeconftest(CONFTEST.read_text(encoding="utf-8"))
    found = outcomes(session("--wiring-set", "two", "--with", "loopback"))
    assert found["test_unloaded"][0] == "skipped", "the extra file made PA5 a loaded pin"
