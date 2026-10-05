"""Wiring options (`--with`) as mappings, their validation and `--board-extra` merging; inline boards only."""

import textwrap

import pytest
import yaml

from hal_st_validation.config import ConfigError, WiringOption, load_board, merge_board, parse_board

BOARD = """
board: x
family: stm32wb55
pins:
  spi1mosi: PA7
  spi1miso: PA6
wiring_sets:
  one:
    options:
      loopback:
        description: jumper PA7 to PA6
        jumpered: {spi1mosi: [spi1miso]}
        loads: [PA6, PA7]
        excludes: [spiloop]
      spiloop:
        jumpered: {PA5: [PB13], PA6: [PB14]}
        loads: [PA5, PA6, PB13, PB14]
        excludes: [loopback]
      i2c:
        jumpered: {PB8: [PC0], PB9: [PC1]}
        loads: [PB8, PB9, PC0, PC1]
        pullups: [PB8, PB9, PC0, PC1]
      plain: a described option
    dio:
      0: PA5
      1: spi1mosi
      3: PB8
      5: PB9
      7: spi1miso
    scope:
      1: {pin: PB8, requires: i2c}
  two:
    options:
      loopback: jumper D11 to D12
      extra:
        jumpered: {PB0: [PB1]}
    dio:
      0: PA5
      2: PB0
"""


def board(text=BOARD):
    return parse_board(yaml.safe_load(text))


def test_options_parse_as_mappings_or_descriptions():
    options = board().wiring_sets["one"].options
    assert options["loopback"] == WiringOption(
        "loopback", "jumper PA7 to PA6", {"PA7": ("PA6",)}, ("PA6", "PA7"), (), frozenset({"spiloop"})
    ), "aliases resolve to pins"
    assert options["i2c"].pullups == ("PB8", "PB9", "PC0", "PC1")
    assert options["plain"] == WiringOption("plain", "a described option")
    assert board().wiring_sets["two"].options["loopback"].description == "jumper D11 to D12"


def test_enabled_options_keep_their_jumpers_apart():
    wiring = board().wiring(["one"], ["i2c"])
    assert set(wiring.options) == {"loopback", "spiloop", "i2c", "plain"}
    assert wiring.option_jumpers == {"i2c": {"PB8": ("PC0",), "PB9": ("PC1",)}}
    assert all(connection.pins in (("PA5",), ("PA7",), ("PB8",), ("PB9",), ("PA6",)) for connection in wiring.connections)
    assert wiring.scope("PB8") == 1, "the connection that requires i2c is enabled"
    assert board().wiring(["one"]).scope("PB8") is None


def test_loaded_pins_are_those_of_enabled_options():
    wiring = board().wiring(["one"], ["i2c", "plain"])
    assert wiring.loaded("PC0") == ("i2c",)
    assert wiring.loaded("PA6") == (), "loopback is offered, not enabled"
    assert wiring.blocking("PC0") == ("i2c",)
    assert wiring.blocking("PC0", {"i2c"}) == ()
    assert board().wiring(["one"], ["loopback"]).loaded("PA7") == ("loopback",)


def test_jumpered_pins_resolve_only_for_the_tag():
    wiring = board().wiring(["one"], ["i2c", "spiloop"])
    assert wiring.dio("PC0") is None
    assert wiring.dio("PC0", allowed={"spiloop"}) is None
    assert wiring.dio("PC0", allowed={"i2c"}) == 3
    assert wiring.dio("PC1", allowed=["i2c"]) == 5
    assert wiring.dio("PB14", allowed={"spiloop"}) == 7
    assert wiring.dio("PA6", allowed={"spiloop"}) == 7, "a pin's own channel comes first"
    assert wiring.jumpered_to("PB13", {"spiloop"}) == {"PA5"}
    assert board().wiring(["one"]).dio("PC0", allowed={"i2c"}) is None, "only enabled options tie pins"


def test_options_of_several_sets_merge():
    text = BOARD.replace(
        "  two:\n    options:\n      loopback: jumper D11 to D12\n", "  two:\n    options:\n      loopback: {loads: [PB0]}\n"
    )
    raw = yaml.safe_load(text)
    raw["wiring_sets"]["two"]["dio"] = {0: "PA5"}
    wiring = parse_board(raw).wiring(["one", "two"], ["loopback"])
    assert wiring.options["loopback"].loads == ("PA6", "PA7", "PB0")
    assert wiring.options["loopback"].description == "jumper PA7 to PA6"
    assert "--with loopback: PA7-PA6; loads PA6, PA7, PB0" in wiring.describe()


@pytest.mark.parametrize(
    ("names", "enabled", "message"),
    [
        (["one"], ["nosuch"], "offer no --with nosuch"),
        (["two"], ["i2c"], "offered: extra, loopback"),
        ([], ["i2c"], "needs the --wiring-set"),
        (["one"], ["loopback", "spiloop"], "exclude each other"),
        (["nosuch"], [], "unknown wiring set"),
    ],
)
def test_invalid_tag_selections(names, enabled, message):
    with pytest.raises(ConfigError, match=message):
        board().wiring(names, enabled)


def test_valid_tag_selections():
    assert board().wiring([], []).enabled == frozenset()
    assert board().wiring(["one"], ["i2c", "spiloop", "plain"]).has("spiloop")
    assert board().wiring(["two"], ["extra"]).dio("PB1", allowed={"extra"}) == 2


@pytest.mark.parametrize(
    "option",
    [
        "{jumpered: [PA5]}",
        "{jumpered: {PA5: PB13}}",
        "{jumpered: {PA5: [PZ1]}}",
        "{loads: PA5}",
        "{pullups: [nosuchalias]}",
        "{excludes: spiloop}",
        "{wires: [PA5]}",
        "[PA5]",
    ],
)
def test_invalid_options(option):
    raw = yaml.safe_load(BOARD)
    raw["wiring_sets"]["two"]["options"]["extra"] = yaml.safe_load(option)
    with pytest.raises(ConfigError):
        parse_board(raw)


def test_requires_names_an_offered_option():
    raw = yaml.safe_load(BOARD)
    raw["wiring_sets"]["two"]["dio"][2] = {"pin": "PB0", "requires": "i2c"}
    with pytest.raises(ConfigError, match="requires option 'i2c'"):
        parse_board(raw)


def test_merge_board():
    base = {"a": {"b": [1, 2], "c": 1, "d": {"e": 1}}, "f": "x", "g": [1]}
    extra = {"a": {"b": [3], "c": 2, "d": "flat", "h": {"i!": [9]}}, "f": {"now": "mapping"}, "g!": [5], "j": None}
    assert merge_board(base, extra) == {
        "a": {"b": [1, 2, 3], "c": 2, "d": "flat", "h": {"i": [9]}},
        "f": {"now": "mapping"},
        "g": [5],
        "j": None,
    }
    assert base == {"a": {"b": [1, 2], "c": 1, "d": {"e": 1}}, "f": "x", "g": [1]}, "the base is not changed"
    assert merge_board({"x": {"y": [1]}}, {"x": {"y!": [2]}}) == {"x": {"y": [2]}}
    assert merge_board({1: "a"}, {1: "b", 2: "c"}) == {1: "b", 2: "c"}, "YAML integer keys (DIO numbers) merge too"


def write(path, text):
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return path


def test_load_board_with_extras(tmp_path):
    base = write(tmp_path / "x.yaml", BOARD)
    first = write(
        tmp_path / "first.yaml",
        """
        wiring_sets:
          one:
            options:
              plain:
                loads: [PB0]
        tests:
          gpio:
            loop_pins: [PA5]
            limit_pins: [PA8]
        """,
    )
    second = write(
        tmp_path / "second.yaml",
        """
        tests:
          gpio:
            loop_pins: [PB0]
            limit_pins!: [PB8]
        """,
    )
    empty = write(tmp_path / "empty.yaml", "")
    loaded = load_board(base, [first, str(second), empty])
    assert loaded.extras == (first, second, empty)
    assert loaded.param("gpio.loop_pins") == ["PA5", "PB0"]
    assert loaded.param("gpio.limit_pins") == ["PB8"]
    assert loaded.wiring(["one"], ["plain"]).loaded("PB0") == ("plain",), "a description became a mapping"
    assert load_board(base).wiring_sets["one"].options["plain"].loads == ()
    with pytest.raises(ConfigError, match="no board extra file"):
        load_board(base, [tmp_path / "missing.yaml"])
    with pytest.raises(ConfigError, match="must be a mapping"):
        load_board(base, [write(tmp_path / "list.yaml", "- a\n")])
    with pytest.raises(ConfigError):
        load_board(base, [write(tmp_path / "bad.yaml", "a: [\n")])


@pytest.mark.parametrize("name", ["nucleo_wb55rg", "nucleo_wba55cg"])
def test_board_files_offer_their_options(name):
    """Each option of the board files parses; every option a set offers can be enabled on its own."""
    loaded = load_board(name)
    for set_name, wiring_set in loaded.wiring_sets.items():
        for tag in wiring_set.options:
            assert loaded.wiring([set_name], [tag]).has(tag)
