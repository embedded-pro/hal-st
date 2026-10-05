"""Shared pytest plumbing: board/wiring options, firmware fixtures, YAML-driven parametrisation.

Parametrisation (see `pytest_generate_tests`): every `board_params` marker is one dimension and every `matrix`
marker adds the dimensions of a YAML mapping; `--depth full` runs their cartesian product and `--depth quick`
(the default) a pairwise subset. `constraint` markers drop combinations the driver cannot take.

`--ad3-serial`, `--no-ad3`, `--fake`, the `ad3` marker and the `ad3` fixture come from the
`ad3_waveforms_bench` pytest plugin; `ad3_settings` below feeds it the board file's AD3 section.

The board file's `known_gaps` mark the HIL tests that run into a driver gap: a gap that aborts or hangs the
firmware skips its tests (run them with `--run-known-gaps`), any other is an expected failure (xfail, not strict).
They describe the firmware, so `--fake` ignores them.
"""

from __future__ import annotations

import os
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest
from ad3_waveforms_bench.pytest_plugin import Ad3Settings
from ad3_waveforms_bench.terminal import FirmwareTerminal, TerminalError

from hal_st_validation.config import BoardConfig, ConfigError, Wiring, known_gap_outcome, load_board
from hal_st_validation.firmware import Firmware, quiesce
from hal_st_validation.pairwise import DEPTHS, combinations

HIL_DIR = Path(__file__).parent / "hil"
_BOARD_KEY = pytest.StashKey[BoardConfig]()


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("hal-st validation")
    group.addoption("--board", default=os.environ.get("HAL_ST_BOARD", "nucleo_wb55rg"), help="board YAML name or path")
    group.addoption("--port", default=os.environ.get("HAL_ST_PORT"), help="firmware terminal serial port; HIL tests skip without it")
    group.addoption("--baud", type=int, default=None, help="terminal baud rate (default from the board YAML)")
    group.addoption(
        "--command-timeout",
        type=float,
        default=float(os.environ["HAL_ST_COMMAND_TIMEOUT"]) if os.environ.get("HAL_ST_COMMAND_TIMEOUT") else None,
        help="seconds to wait for a command's reply (default from the board YAML); raise it for slow links such as port-bridge",
    )
    group.addoption("--wiring-set", default=os.environ.get("HAL_ST_WIRING", ""), help="comma separated wiring sets from the board YAML")
    group.addoption("--with", dest="with_tags", action="append", default=[], help="enable an optional wiring tag (repeatable)")
    group.addoption("--set", dest="overrides", action="append", default=[], help="override a test parameter: pwm.waveform.freq=[20000]")
    group.addoption(
        "--run-known-gaps",
        action="store_true",
        help="also run the tests of known gaps that abort or hang the firmware (they are skipped by default)",
    )
    group.addoption(
        "--depth",
        choices=DEPTHS,
        default=os.environ.get("HAL_ST_DEPTH", "quick"),
        help="quick: pairwise subset of every parameter matrix (default); full: complete cartesian products",
    )


def board_config(config: pytest.Config) -> BoardConfig:
    if _BOARD_KEY not in config.stash:
        board = load_board(config.getoption("--board"))
        board.apply_overrides(config.getoption("overrides"))
        config.stash[_BOARD_KEY] = board
    return config.stash[_BOARD_KEY]


def _ids(value: Any) -> str:
    if isinstance(value, dict):
        if "name" in value:
            return str(value["name"])
        if "index" in value:
            return f"index{value['index']}"
        return "-".join(f"{key}={value[key]}" for key in value)
    if isinstance(value, (list, tuple)):
        return ":".join(str(item) for item in value)
    return str(value)


class _Dimension:
    """One axis of a test's parameter space: `names` are the argnames it sets, `values` one tuple per option."""

    def __init__(self, label: str, names: list[str], values: list[tuple[Any, ...]], ids: list[str]) -> None:
        self.label = label
        self.names = names
        self.values = values
        self.ids = ids


def _split(value: Any, names: list[str]) -> tuple[Any, ...]:
    if len(names) == 1:
        return (value,)
    if isinstance(value, dict):
        return tuple(value.get(name) for name in names)
    return tuple(value)


def _dimensions(metafunc: pytest.Metafunc, board: BoardConfig) -> tuple[list[_Dimension], str | None]:
    """The dimensions of the `board_params`/`matrix` markers (in source order) and a skip reason."""
    dimensions: list[_Dimension] = []
    markers = [marker for marker in metafunc.definition.iter_markers() if marker.name in ("board_params", "matrix")]
    for marker in reversed(markers):
        if marker.name == "matrix":
            path = marker.args[0]
            if board.param(path, None) is None:
                return dimensions, f"tests.{path} not configured"
            for name, values in board.matrix(path).items():
                dimensions.append(_Dimension(name, [name], [(value,) for value in values], [f"{name}={_ids(value)}" for value in values]))
            continue
        argnames = marker.args[0]
        names = [name.strip() for name in argnames.split(",")]
        if "values" in marker.kwargs:
            values = marker.kwargs["values"]
        else:
            path = marker.args[1]
            values = board.param(path, None)
            if values is None:
                return dimensions, f"tests.{path} not configured"
        if not isinstance(values, (list, tuple)):
            values = [values]
        if not values:
            return dimensions, f"no values for {argnames}"
        dimensions.append(_Dimension(argnames, names, [_split(value, names) for value in values], [_ids(value) for value in values]))
    return dimensions, None


def pytest_generate_tests(metafunc: pytest.Metafunc) -> None:
    """`@pytest.mark.board_params("freq", "pwm.frequencies")` adds one dimension from the board YAML (or from
    `values=[...]`); for several argnames each item is a mapping (picked by name) or a sequence (positional).
    `@pytest.mark.matrix("pwm.waveform")` adds one dimension per key of a YAML mapping, named like the argnames.
    `@pytest.mark.constraint(valid=predicate)` keeps a combination only when `predicate(values)` is true; it receives
    a possibly partial `argname -> value` mapping and must return False only for impossible assignments.
    """
    try:
        board = board_config(metafunc.config)
        dimensions, skip = _dimensions(metafunc, board)
    except ConfigError as error:
        raise pytest.UsageError(str(error)) from error
    if not dimensions and skip is None:
        return
    argnames = [name for dimension in dimensions for name in dimension.names]
    if skip is not None:
        names = argnames + [name for name in _pending_names(metafunc) if name not in argnames]
        metafunc.parametrize(names, [pytest.param(*([None] * len(names)), marks=pytest.mark.skip(reason=skip))])
        return
    duplicates = {name for name in argnames if argnames.count(name) > 1}
    if duplicates:
        raise pytest.UsageError(f"{metafunc.definition.nodeid}: argnames set twice: {sorted(duplicates)}")
    missing = [name for name in argnames if name not in metafunc.fixturenames]
    if missing:
        raise pytest.UsageError(f"{metafunc.definition.nodeid}: no argument for {missing}")
    predicates = [marker.kwargs["valid"] for marker in metafunc.definition.iter_markers("constraint")]
    by_label = {dimension.label: dimension for dimension in dimensions}

    def flatten(assignment: dict[str, Any]) -> dict[str, Any]:
        flat: dict[str, Any] = {}
        for label, index in assignment.items():
            flat.update(zip(by_label[label].names, by_label[label].values[index]))
        return flat

    def valid(assignment: dict[str, Any]) -> bool:
        flat = flatten(assignment)
        return all(predicate(flat) for predicate in predicates)

    space = {dimension.label: list(range(len(dimension.values))) for dimension in dimensions}
    chosen = combinations(space, metafunc.config.getoption("--depth"), valid)
    params = []
    for assignment in chosen:
        values = [value for dimension in dimensions for value in dimension.values[assignment[dimension.label]]]
        ids = "-".join(dimension.ids[assignment[dimension.label]] for dimension in dimensions)
        params.append(pytest.param(*values, id=ids))
    if not params:
        params = [pytest.param(*([None] * len(argnames)), marks=pytest.mark.skip(reason="no valid combination"))]
    metafunc.parametrize(argnames, params)


def _pending_names(metafunc: pytest.Metafunc) -> list[str]:
    """Arguments that no fixture provides: the ones a skipped parametrisation still has to set."""
    fixtures = getattr(metafunc, "_arg2fixturedefs", {})
    return [name for name in metafunc.fixturenames if name not in fixtures and name != "request"]


def hil_test_id(item: pytest.Item) -> str:
    """The node id relative to tests/hil (`test_pwm.py::test_waveform[...]`), which `known_gaps` patterns match."""
    relative = Path(str(item.path)).relative_to(HIL_DIR).as_posix()
    return f"{relative}::{item.nodeid.partition('::')[2]}"


def _apply_known_gaps(config: pytest.Config, item: pytest.Item, board: BoardConfig | None) -> None:
    if board is None or config.getoption("--fake"):
        return
    outcome = known_gap_outcome(board.known_gaps, hil_test_id(item), config.getoption("--run-known-gaps"))
    if outcome is None:
        return
    kind, reason = outcome
    item.add_marker(pytest.mark.skip(reason=reason) if kind == "skip" else pytest.mark.xfail(strict=False, reason=reason))


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    port = config.getoption("--port")
    tags = set(config.getoption("with_tags"))
    try:
        board: BoardConfig | None = board_config(config)
    except ConfigError:
        board = None
    family = None if board is None else board.family
    for item in items:
        if HIL_DIR in Path(str(item.path)).parents:
            item.add_marker(pytest.mark.hil)
            _apply_known_gaps(config, item, board)
        if item.get_closest_marker("hil") and not (port or config.getoption("--fake")):
            item.add_marker(pytest.mark.skip(reason="HIL test: pass --port"))
        marker = item.get_closest_marker("family")
        if marker and family and marker.args[0] != family:
            item.add_marker(pytest.mark.skip(reason=f"only for {marker.args[0]}"))
        marker = item.get_closest_marker("requires_option")
        if marker and marker.args[0] not in tags:
            item.add_marker(pytest.mark.skip(reason=f"enable with --with {marker.args[0]}"))


@pytest.fixture(scope="session")
def board_cfg(pytestconfig: pytest.Config) -> BoardConfig:
    return board_config(pytestconfig)


@pytest.fixture(scope="session")
def wiring(pytestconfig: pytest.Config, board_cfg: BoardConfig) -> Wiring:
    names = [name.strip() for name in pytestconfig.getoption("--wiring-set").split(",") if name.strip()]
    return board_cfg.wiring(names, pytestconfig.getoption("with_tags"))


class Need:
    """Wiring lookups that skip the test when the active wiring lacks the connection."""

    def __init__(self, wiring: Wiring, board: BoardConfig) -> None:
        self.wiring = wiring
        self.board = board

    def _found(self, value: int | None, what: str) -> int:
        if value is None:
            pytest.skip(f"{what} is not wired in wiring set(s) {', '.join(self.wiring.sets) or '(none)'}")
        return value

    def dio(self, pin: str | None = None, role: str | None = None) -> int:
        resolved = None if pin is None else self.board.resolve_pin(pin)
        return self._found(self.wiring.dio(resolved, role), f"DIO for {pin or role}")

    def wavegen(self, pin: str | None = None, role: str | None = None) -> int:
        resolved = None if pin is None else self.board.resolve_pin(pin)
        return self._found(self.wiring.wavegen(resolved, role), f"wavegen for {pin or role}")

    def scope(self, pin: str | None = None, role: str | None = None) -> int:
        resolved = None if pin is None else self.board.resolve_pin(pin)
        return self._found(self.wiring.scope(resolved, role), f"scope for {pin or role}")

    def optional_scope(self, pin: str) -> int | None:
        return self.wiring.scope(self.board.resolve_pin(pin))

    def optional_dio(self, pin: str | None = None, role: str | None = None) -> int | None:
        resolved = None if pin is None else self.board.resolve_pin(pin)
        return self.wiring.dio(resolved, role)

    def tag(self, tag: str) -> None:
        if not self.wiring.has(tag):
            pytest.skip(f"enable with --with {tag}")


@pytest.fixture(scope="session")
def need(wiring: Wiring, board_cfg: BoardConfig) -> Need:
    return Need(wiring, board_cfg)


@pytest.fixture(scope="session")
def depth(pytestconfig: pytest.Config) -> str:
    """`--depth`: tests that loop over YAML lists internally use the first entry only with `quick`."""
    return pytestconfig.getoption("--depth")


@pytest.fixture(scope="session")
def terminal(pytestconfig: pytest.Config, board_cfg: BoardConfig) -> Iterator[FirmwareTerminal]:
    port = pytestconfig.getoption("--port")
    serial = None
    if pytestconfig.getoption("--fake"):
        from hal_st_validation.fake_firmware import FakeFirmware, FakeSerial

        # Final lines end with a line break, as the HIL terminal prints them while processing a command, so
        # the fake's time-driven events (watchdog warnings) cannot join a final line and its prompt.
        fake = FakeFirmware(
            board=board_cfg.firmware_name or board_cfg.name,
            family=board_cfg.family,
            sysclk=board_cfg.sysclk or 0,
            pins=dict(board_cfg.pins),
            style="line",
        )
        serial = FakeSerial(fake)
    elif not port:
        pytest.skip("pass --port")
    baud = pytestconfig.getoption("--baud") or board_cfg.terminal.baud
    timeout = pytestconfig.getoption("--command-timeout") or board_cfg.terminal.command_timeout
    with FirmwareTerminal(
        port,
        baud,
        timeout=timeout,
        serial=serial,
        max_command_length=board_cfg.terminal.max_command_length,
    ) as term:
        # A link with latency (port-bridge, ST-LINK VCP) can deliver a reply after the next command was written,
        # which would hand every later command its predecessor's reply: sync with the full command timeout and
        # let the line go quiet so the session starts with no reply in flight.
        try:
            term.sync(timeout=timeout)
        except TerminalError as error:
            pytest.exit(f"firmware on {port} does not answer ping: {error}", returncode=3)
        quiesce(term, quiet=0.3)
        yield term


@pytest.fixture(scope="session")
def fw(terminal: FirmwareTerminal, board_cfg: BoardConfig) -> Firmware:
    firmware = Firmware(terminal, board_cfg.pins)
    info = firmware.system.info()
    if not board_cfg.matches_firmware_name(info.board):
        pytest.exit(f"--board {board_cfg.name} but the firmware reports {info.board}", returncode=3)
    return firmware


@pytest.fixture(scope="session")
def ad3_settings(board_cfg: BoardConfig) -> Ad3Settings:
    return Ad3Settings(
        analog_limits=(board_cfg.ad3.analog_min, board_cfg.ad3.analog_max),
        vplus=board_cfg.ad3.vplus,
        vminus=board_cfg.ad3.vminus,
    )


@pytest.fixture(autouse=True)
def _hil_isolation(request: pytest.FixtureRequest) -> Iterator[None]:
    """Clear stale events before a HIL test; afterwards close what it opened and reset the AD3 outputs."""
    if request.node.get_closest_marker("hil") is None:
        yield
        return
    firmware: Firmware = request.getfixturevalue("fw")
    quiesce(firmware.terminal)
    firmware.terminal.drain_events()
    yield
    boots = firmware.terminal.drain_events("boot")
    failures = firmware.close_all()
    if "ad3" in request.fixturenames:
        request.getfixturevalue("ad3").reset_outputs()
    if boots and request.node.get_closest_marker("resets_board") is None:
        pytest.fail(f"unexpected reset during the test: {boots[-1].raw}")
    if failures:
        pytest.fail(f"cleanup failed: {failures}")
