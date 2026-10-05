"""Shared pytest plumbing: board/wiring options, firmware fixtures, YAML-driven parametrisation.

Parametrisation (see `pytest_generate_tests`): every `board_params` marker is one dimension and every `matrix`
marker adds the dimensions of a YAML mapping; `--depth full` runs their cartesian product and `--depth quick`
(the default) a pairwise subset. `constraint` markers drop combinations the driver cannot take.

`--ad3-serial`, `--no-ad3`, `--fake`, the `ad3` marker and the `ad3` fixture come from the
`ad3_waveforms_bench` pytest plugin; `ad3_settings` below feeds it the board file's AD3 section.

`--board-extra` files are deep-merged over the board file (`config.merge_board`). `--with` options must be
offered by a `--wiring-set` and must not exclude each other, else the run stops with a usage error. A pin an
enabled option loads (`loads:`) or ties to another pin (`jumpered:`) is only resolved by `need` for tests marked
`uses_option(<tag>)` or `requires_option(<tag>)`; the others skip ("pin X loaded by --with T").

The board file's `known_gaps` mark the HIL tests that run into a driver gap: a gap that aborts or hangs the
firmware skips its tests (run them with `--run-known-gaps`), any other is an expected failure (xfail, not strict).
They describe the firmware, so `--fake` ignores them.
"""

from __future__ import annotations

import contextlib
import os
from collections.abc import Iterable, Iterator
from pathlib import Path
from typing import Any

import pytest
from ad3_waveforms_bench.pytest_plugin import Ad3Settings
from ad3_waveforms_bench.terminal import FirmwareTerminal, TerminalError

from hal_st_validation.config import BoardConfig, ChannelKind, ConfigError, Connection, Wiring, known_gap_outcome, load_board
from hal_st_validation.firmware import Firmware, quiesce
from hal_st_validation.pairwise import DEPTHS, combinations

HIL_DIR = Path(__file__).parent / "hil"
_BOARD_KEY = pytest.StashKey[BoardConfig]()
_WIRING_KEY = pytest.StashKey[Wiring]()
_OPTION_MARKERS = ("uses_option", "requires_option")


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("hal-st validation")
    group.addoption("--board", default=os.environ.get("HAL_ST_BOARD", "nucleo_wb55rg"), help="board YAML name or path")
    group.addoption(
        "--board-extra",
        dest="board_extras",
        action="append",
        default=[path for path in os.environ.get("HAL_ST_BOARD_EXTRA", "").split(os.pathsep) if path],
        help="YAML deep-merged over the board file (repeatable; or HAL_ST_BOARD_EXTRA, a list separated by os.pathsep)",
    )
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
        board = load_board(config.getoption("--board"), config.getoption("board_extras"))
        board.apply_overrides(config.getoption("overrides"))
        config.stash[_BOARD_KEY] = board
    return config.stash[_BOARD_KEY]


def wiring_config(config: pytest.Config) -> Wiring:
    """`--wiring-set` with the `--with` options, validated once (`BoardConfig.wiring`)."""
    if _WIRING_KEY not in config.stash:
        names = [name.strip() for name in config.getoption("--wiring-set").split(",") if name.strip()]
        config.stash[_WIRING_KEY] = board_config(config).wiring(names, config.getoption("with_tags"))
    return config.stash[_WIRING_KEY]


def option_tags(node: pytest.Item) -> frozenset[str]:
    """The options a test handles: its `uses_option` and `requires_option` markers."""
    return frozenset(str(marker.args[0]) for name in _OPTION_MARKERS for marker in node.iter_markers(name))


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


def _parametrize_options(metafunc: pytest.Metafunc, marker: pytest.Mark) -> None:
    """`@pytest.mark.wiring_options("tag")` runs the test once per option the selected wiring sets offer: the
    enabled ones (default), or with `enabled=False` the ones left out; each case is marked `uses_option(tag)`."""
    argname = marker.args[0]
    enabled = marker.kwargs.get("enabled", True)
    try:
        wiring = wiring_config(metafunc.config)
    except ConfigError as error:
        raise pytest.UsageError(str(error)) from error
    tags = [tag for tag in wiring.options if (tag in wiring.enabled) == enabled]
    if not tags:
        reason = "no option enabled with --with" if enabled else "every offered option is enabled"
        metafunc.parametrize(argname, [pytest.param(None, marks=pytest.mark.skip(reason=reason))])
        return
    metafunc.parametrize(argname, [pytest.param(tag, id=tag, marks=pytest.mark.uses_option(tag)) for tag in tags])


def pytest_generate_tests(metafunc: pytest.Metafunc) -> None:
    """`@pytest.mark.board_params("freq", "pwm.frequencies")` adds one dimension from the board YAML (or from
    `values=[...]`); for several argnames each item is a mapping (picked by name) or a sequence (positional).
    `@pytest.mark.matrix("pwm.waveform")` adds one dimension per key of a YAML mapping, named like the argnames.
    `@pytest.mark.constraint(valid=predicate)` keeps a combination only when `predicate(values)` is true; it receives
    a possibly partial `argname -> value` mapping and must return False only for impossible assignments.
    `@pytest.mark.wiring_options(argname)` parametrizes over wiring options instead (`_parametrize_options`).
    """
    options = metafunc.definition.get_closest_marker("wiring_options")
    if options is not None:
        _parametrize_options(metafunc, options)
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
    if board is not None:
        try:
            wiring_config(config)
        except ConfigError as error:
            raise pytest.UsageError(str(error)) from error
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
        for marker in item.iter_markers("requires_option"):
            if marker.args[0] not in tags:
                item.add_marker(pytest.mark.skip(reason=f"enable with --with {marker.args[0]}"))
        for marker in item.iter_markers("conflicts_option"):
            if marker.args[0] in tags:
                item.add_marker(pytest.mark.skip(reason=f"disconnect --with {marker.args[0]}"))


@pytest.fixture(scope="session")
def board_cfg(pytestconfig: pytest.Config) -> BoardConfig:
    return board_config(pytestconfig)


@pytest.fixture(scope="session")
def wiring(pytestconfig: pytest.Config) -> Wiring:
    return wiring_config(pytestconfig)


class Need:
    """Wiring lookups that skip the test when the active wiring lacks the connection, or when the pin is loaded by
    an enabled option the test does not handle (`allowed`: the tags of its `uses_option`/`requires_option`
    markers). Pins an enabled option ties to a channel by a jumper resolve only for the tags in `allowed`."""

    def __init__(self, wiring: Wiring, board: BoardConfig, allowed: Iterable[str] = ()) -> None:
        self.wiring = wiring
        self.board = board
        self.allowed = frozenset(allowed)

    def _blocked(self, pins: Iterable[str]) -> str | None:
        for pin in pins:
            tags = self.wiring.blocking(pin, self.allowed)
            if tags:
                return f'pin {pin} loaded by --with {tags[0]} (mark the test uses_option("{tags[0]}") if it handles that wiring)'
        return None

    def _lookup(self, kind: ChannelKind, pin: str | None, role: str | None) -> tuple[Connection | None, str | None]:
        """The connection and, when the test must leave its pins alone, why."""
        resolved = None if pin is None else self.board.resolve_pin(pin)
        connection = self.wiring.connection(kind, resolved, role, self.allowed)
        if connection is None:
            return None, None
        pins = ([resolved] if resolved is not None else []) + list(connection.pins)
        return connection, self._blocked(pins)

    def _required(self, kind: ChannelKind, pin: str | None, role: str | None, what: str) -> int:
        connection, blocked = self._lookup(kind, pin, role)
        if connection is None:
            pytest.skip(f"{what} for {pin or role} is not wired in wiring set(s) {', '.join(self.wiring.sets) or '(none)'}")
        if blocked is not None:
            pytest.skip(blocked)
        assert connection is not None
        return connection.channel

    def _optional(self, kind: ChannelKind, pin: str | None, role: str | None) -> int | None:
        connection, blocked = self._lookup(kind, pin, role)
        return None if connection is None or blocked is not None else connection.channel

    def dio(self, pin: str | None = None, role: str | None = None) -> int:
        return self._required("dio", pin, role, "DIO")

    def wavegen(self, pin: str | None = None, role: str | None = None) -> int:
        return self._required("wavegen", pin, role, "wavegen")

    def scope(self, pin: str | None = None, role: str | None = None) -> int:
        return self._required("scope", pin, role, "scope")

    def optional_scope(self, pin: str) -> int | None:
        return self._optional("scope", pin, None)

    def optional_dio(self, pin: str | None = None, role: str | None = None) -> int | None:
        return self._optional("dio", pin, role)

    def unloaded(self, *pins: str) -> None:
        """Skip when an enabled option the test does not handle loads one of `pins` (for pins used without an AD3
        channel)."""
        blocked = self._blocked(self.board.resolve_pin(pin) for pin in pins)
        if blocked is not None:
            pytest.skip(blocked)

    def tag(self, tag: str) -> None:
        if not self.wiring.has(tag):
            pytest.skip(f"enable with --with {tag}")


@pytest.fixture
def need(request: pytest.FixtureRequest, wiring: Wiring, board_cfg: BoardConfig) -> Need:
    return Need(wiring, board_cfg, option_tags(request.node))


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


def release_ad3(ad3: Any) -> None:
    """Outputs off (`reset_outputs()`), then the weak pulls off and the I2C and SPI protocol engines reset, which
    `reset_outputs()` leaves as they are. A device or runtime without them (or the `--fake` API) is fine."""
    ad3.reset_outputs()
    with contextlib.suppress(AttributeError, RuntimeError):
        ad3.dio.pull()
    for name in ("FDwfDigitalI2cReset", "FDwfDigitalSpiReset"):
        with contextlib.suppress(AttributeError, RuntimeError):
            getattr(ad3.api, name)(ad3.handle)


@pytest.fixture
def ad3_released(request: pytest.FixtureRequest) -> None:
    """The AD3 outputs and pulls off before the test, so the pins see only the board and its wiring; without
    `--no-ad3` this needs the AD3 (the test skips when none is found)."""
    if not request.config.getoption("--no-ad3"):
        release_ad3(request.getfixturevalue("ad3"))


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
        release_ad3(request.getfixturevalue("ad3"))
    if boots and request.node.get_closest_marker("resets_board") is None:
        pytest.fail(f"unexpected reset during the test: {boots[-1].raw}")
    if failures:
        pytest.fail(f"cleanup failed: {failures}")
