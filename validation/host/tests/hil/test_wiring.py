"""Self-check of the bench wiring with GPIO commands only: run it first after wiring the board
(`pytest tests/hil/test_wiring.py --port ... --wiring-set <set> [--with <tag> ...]`).

With the AD3 outputs and pulls off, the MCU drives the key pin of each `jumpered` entry of an enabled option and
reads the listed pins against the opposite MCU pull (continuity), reads the `pullups` pins against the MCU
pull-down (external pull-ups on a live 3V3 rail), looks for the jumpers of offered options that are not enabled
(fitted wiring the run does not know about), and checks that the pins of `tests.wiring.undriven` follow both MCU
pulls (nothing else, such as an ST-LINK line through a solder bridge, drives them). Skipped with `--fake`: the
fake firmware has no wiring.

Scenarios: features/wiring.feature.
"""

from __future__ import annotations

import pytest
from pytest_bdd import given, scenario, then

from hal_st_validation.config import Wiring
from hal_st_validation.firmware import Firmware


@pytest.fixture(autouse=True)
def _physical_wiring(request: pytest.FixtureRequest) -> None:
    if request.config.getoption("--fake"):
        pytest.skip("checks the physical wiring: nothing to check with --fake")
    request.getfixturevalue("ad3_released")


def level(fw: Firmware, pin: str, pull: str) -> int:
    fw.gpio.cfg(pin, "in", pull=pull)
    try:
        return fw.gpio.get(pin)
    finally:
        fw.gpio.release(pin)


def follows(fw: Firmware, key: str, pin: str) -> bool:
    """`pin` reads 1 against its pull-down while `key` drives high, and 0 against its pull-up while `key` drives low."""
    fw.gpio.cfg(key, "out")
    try:
        results = []
        for driven, pull in ((1, "down"), (0, "up")):
            fw.gpio.set(key, driven)
            results.append(level(fw, pin, pull) == driven)
        return all(results)
    finally:
        fw.gpio.release(key)


def jumper_pairs(wiring: Wiring, tag: str) -> list[tuple[str, str]]:
    return [(key, pin) for key, pins in wiring.options[tag].jumpered.items() for pin in pins]


@pytest.mark.wiring_options("tag")
@scenario("wiring.feature", "The jumpers of every enabled option are fitted")
def test_option_continuity(tag):
    pass


@pytest.mark.wiring_options("tag")
@scenario("wiring.feature", "The pull-ups of every enabled option reach a live 3V3 rail")
def test_option_pullups(tag):
    pass


@pytest.mark.wiring_options("tag", enabled=False)
@scenario("wiring.feature", "No wiring of an option left out is fitted")
def test_undeclared_options(tag):
    pass


@pytest.mark.board_params("pin", "wiring.undriven")
@scenario("wiring.feature", "Nothing else drives the undriven pins")
def test_no_foreign_drivers(pin):
    pass


@given("the jumpered pin pairs of the option, skipping if it declares none", target_fixture="pairs")
def enabled_pairs(wiring, tag):
    pairs = jumper_pairs(wiring, tag)
    if not pairs:
        pytest.skip(f"--with {tag} declares no jumpered pins")
    return pairs


@given("the jumpered pin pairs of the option, skipping if it declares none to look for", target_fixture="pairs")
def left_out_pairs(wiring, tag):
    pairs = jumper_pairs(wiring, tag)
    if not pairs:
        pytest.skip(f"--with {tag} declares no jumpered pins to look for")
    return pairs


@given("the pull-up pins of the option, skipping if it declares none", target_fixture="pullups")
def option_pullups(wiring, tag):
    pullups = wiring.options[tag].pullups
    if not pullups:
        pytest.skip(f"--with {tag} declares no pull-ups")
    return pullups


@given("the pin, resolved, unless an enabled option loads it", target_fixture="resolved")
def unloaded_pin(wiring, board_cfg, pin):
    resolved = board_cfg.resolve_pin(pin)
    loads = wiring.loaded(resolved)
    if loads:
        pytest.skip(f"pin {resolved} loaded by --with {loads[0]}")
    return resolved


@then("the listed pin of every pair follows its key pin")
def continuity(fw, tag, pairs):
    broken = [f"{key}-{pin}" for key, pin in pairs if not follows(fw, key, pin)]
    assert not broken, f"--with {tag}: no continuity on {', '.join(broken)}: check those jumpers"


@then("every pull-up pin reads 1 against the MCU pull-down")
def pulled_up(fw, tag, pullups):
    low = [pin for pin in pullups if level(fw, pin, "down") != 1]
    assert not low, f"--with {tag}: {', '.join(low)} read 0 against the MCU pull-down: no pull-up to a live 3V3 rail"


@then("no listed pin of a pair follows its key pin")
def nothing_fitted(fw, tag, pairs):
    fitted = [f"{key}-{pin}" for key, pin in pairs if follows(fw, key, pin)]
    assert not fitted, f"the wiring of --with {tag} is fitted ({', '.join(fitted)}): pass --with {tag} or remove the wiring"


@then("the pin reads 1 against the MCU pull-up and 0 against the MCU pull-down")
def undriven(fw, resolved):
    levels = (level(fw, resolved, "up"), level(fw, resolved, "down"))
    assert levels == (1, 0), (
        f"{resolved} reads {levels[0]} against the MCU pull-up and {levels[1]} against the pull-down: something drives it "
        "(a solder bridge to an ST-LINK line, a jumper or an AD3 output)"
    )
