"""Board description and AD3 wiring loaded from `boards/<board>.yaml`."""

from __future__ import annotations

import copy
import functools
import re
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any, Literal

import yaml

from .protocol import is_alias, normalize_pin

BOARDS_DIR = Path(__file__).resolve().parent.parent / "boards"
ChannelKind = Literal["dio", "wavegen", "scope"]
_KINDS: tuple[ChannelKind, ...] = ("dio", "wavegen", "scope")
_OPTION_KEYS = frozenset({"description", "jumpered", "loads", "pullups", "excludes"})
_MISSING = object()


class ConfigError(ValueError):
    pass


@dataclass(frozen=True)
class Connection:
    """One AD3 channel wired to one firmware pin."""

    kind: ChannelKind
    channel: int
    pin: str | None
    role: str | None = None
    note: str = ""
    requires: str | None = None
    # Pins tied to `pin` by a jumper: the channel reaches them as well.
    jumpered: tuple[str, ...] = ()

    @property
    def pins(self) -> tuple[str, ...]:
        return ((self.pin,) if self.pin else ()) + self.jumpered

    def describe(self) -> str:
        name = {"dio": "DIO", "wavegen": "W", "scope": "Scope "}[self.kind]
        parts = [f"{name}{self.channel}", "+".join(self.pins) or "-"]
        if self.role:
            parts.append(f"({self.role})")
        if self.requires:
            parts.append(f"[--with {self.requires}]")
        if self.note:
            parts.append(f"- {self.note}")
        return " ".join(parts)


@dataclass(frozen=True)
class WiringOption:
    """Optional wiring a set offers (`--with <tag>`): `jumpered` ties each key pin to the listed pins (the key is the
    end the wiring self-check drives), `loads` are pins the wiring loads, `pullups` pins it pulls up, and `excludes`
    names options that cannot be fitted at the same time."""

    tag: str
    description: str = ""
    jumpered: Mapping[str, tuple[str, ...]] = field(default_factory=dict)
    loads: tuple[str, ...] = ()
    pullups: tuple[str, ...] = ()
    excludes: frozenset[str] = frozenset()

    def merged(self, other: WiringOption) -> WiringOption:
        """The option as two selected sets offer it together."""
        jumpered = {pin: tuple(dict.fromkeys((*self.jumpered.get(pin, ()), *others))) for pin, others in other.jumpered.items()}
        return WiringOption(
            tag=self.tag,
            description=self.description or other.description,
            jumpered={**self.jumpered, **jumpered},
            loads=tuple(dict.fromkeys((*self.loads, *other.loads))),
            pullups=tuple(dict.fromkeys((*self.pullups, *other.pullups))),
            excludes=self.excludes | other.excludes,
        )


@dataclass(frozen=True)
class WiringSet:
    name: str
    description: str
    connections: tuple[Connection, ...]
    options: Mapping[str, WiringOption] = field(default_factory=dict)
    jumpers: tuple[str, ...] = ()


@dataclass(frozen=True)
class Wiring:
    """The merged active wiring sets, restricted to the optional connections that are enabled.

    `options` are the options the selected sets offer, `enabled` the tags of `--with`; `option_jumpers` holds the
    `jumpered` pins of the enabled options. A channel reaches an option-jumpered pin only for lookups that pass
    the tag in `allowed` (tests marked `uses_option`/`requires_option`), and pins an enabled option loads are
    reported by `loaded()` so such lookups can be refused for other tests."""

    sets: tuple[str, ...]
    connections: tuple[Connection, ...]
    enabled: frozenset[str]
    jumpers: tuple[str, ...] = ()
    options: Mapping[str, WiringOption] = field(default_factory=dict)
    option_jumpers: Mapping[str, Mapping[str, tuple[str, ...]]] = field(default_factory=dict)

    def connection(
        self, kind: ChannelKind, pin: str | None = None, role: str | None = None, allowed: Iterable[str] = ()
    ) -> Connection | None:
        """The first connection of `kind` (and `role`) on `pin`: its own pins first, then the pins the jumpers of the
        enabled options in `allowed` tie to them."""
        candidates = [c for c in self.connections if c.kind == kind and (role is None or c.role == role)]
        if pin is None:
            return candidates[0] if candidates else None
        for connection in candidates:
            if pin in connection.pins:
                return connection
        reach = self.jumpered_to(pin, allowed)
        return next((connection for connection in candidates if reach & set(connection.pins)), None)

    def channel(self, kind: ChannelKind, pin: str | None = None, role: str | None = None, allowed: Iterable[str] = ()) -> int | None:
        connection = self.connection(kind, pin, role, allowed)
        return None if connection is None else connection.channel

    def dio(self, pin: str | None = None, role: str | None = None, allowed: Iterable[str] = ()) -> int | None:
        return self.channel("dio", pin, role, allowed)

    def wavegen(self, pin: str | None = None, role: str | None = None, allowed: Iterable[str] = ()) -> int | None:
        return self.channel("wavegen", pin, role, allowed)

    def scope(self, pin: str | None = None, role: str | None = None, allowed: Iterable[str] = ()) -> int | None:
        return self.channel("scope", pin, role, allowed)

    def jumpered_to(self, pin: str, allowed: Iterable[str] = ()) -> set[str]:
        """Pins the jumpers of the enabled options in `allowed` tie to `pin`."""
        tags = set(allowed)
        reach: set[str] = set()
        for tag, jumpers in self.option_jumpers.items():
            if tag not in tags:
                continue
            for key, others in jumpers.items():
                net = {key, *others}
                if pin in net:
                    reach |= net - {pin}
        return reach

    def loaded(self, pin: str) -> tuple[str, ...]:
        """The enabled options that load `pin`."""
        return tuple(tag for tag, option in self.options.items() if tag in self.enabled and pin in option.loads)

    def blocking(self, pin: str, allowed: Iterable[str] = ()) -> tuple[str, ...]:
        """The enabled options that load `pin` and are not in `allowed`: a test outside them must leave it alone."""
        tags = set(allowed)
        return tuple(tag for tag in self.loaded(pin) if tag not in tags)

    def has(self, tag: str) -> bool:
        return tag in self.enabled

    def describe(self) -> str:
        lines = [f"wiring sets: {', '.join(self.sets) or '(none)'}"]
        lines += [f"  {connection.describe()}" for connection in self.connections]
        lines += [f"  jumper: {jumper}" for jumper in self.jumpers]
        for tag in sorted(self.enabled):
            option = self.options[tag]
            parts = [f"{key}-{'+'.join(others)}" for key, others in option.jumpered.items()]
            if option.loads:
                parts.append(f"loads {', '.join(option.loads)}")
            lines.append(f"  --with {tag}: {'; '.join(parts) or option.description or 'enabled'}")
        return "\n".join(lines)


@dataclass(frozen=True)
class TerminalConfig:
    baud: int = 921600
    interface: str = ""
    uart: int | None = None
    pins: tuple[str, ...] = ()
    max_command_length: int = 255
    command_timeout: float = 2.0
    boot_timeout: float = 5.0


@functools.cache
def _gap_regex(pattern: str) -> re.Pattern[str]:
    """`*` matches any text and `?` one character; everything else, `[` and `]` of parameter ids included, is
    literal (unlike fnmatch, whose brackets are character classes)."""
    return re.compile("".join(".*" if char == "*" else "." if char == "?" else re.escape(char) for char in pattern))


@dataclass(frozen=True)
class KnownGap:
    """A driver gap of the board (`known_gaps` in the board file) that HIL tests run into.

    `tests` are patterns on the test id relative to tests/hil (`test_pwm.py::test_waveform[*mode=center*]`), `reason`
    names the code and the symptom, and `hangs` marks gaps that abort or hang the firmware (such tests are skipped
    unless `--run-known-gaps`; the others are expected to fail)."""

    tests: tuple[str, ...]
    reason: str
    hangs: bool = False

    def matches(self, test_id: str) -> bool:
        return any(_gap_regex(pattern).fullmatch(test_id) for pattern in self.tests)


def known_gap_outcome(gaps: Iterable[KnownGap], test_id: str, run_hanging: bool = False) -> tuple[str, str] | None:
    """How a HIL test meets the known gaps: `("skip", reason)` when a matching gap hangs the firmware (unless
    `run_hanging`), `("xfail", reason)` for any other match, None when no gap matches."""
    matched = [gap for gap in gaps if gap.matches(test_id)]
    if not matched:
        return None
    hanging = [gap for gap in matched if gap.hangs]
    if hanging and not run_hanging:
        return "skip", "known gap (firmware aborts/hangs): " + "; ".join(gap.reason for gap in hanging)
    return "xfail", "known gap: " + "; ".join(gap.reason for gap in matched)


@dataclass(frozen=True)
class Ad3Config:
    vplus: float | None = None
    vminus: float | None = None
    analog_min: float = 0.0
    analog_max: float = 3.3


@dataclass
class BoardConfig:
    name: str
    family: str
    description: str
    sysclk: int | None
    terminal: TerminalConfig
    pins: dict[str, str]
    ad3: Ad3Config
    wiring_sets: dict[str, WiringSet]
    tests: dict[str, Any]
    path: Path | None = None
    # `--board-extra` files merged over the board file, in order.
    extras: tuple[Path, ...] = ()
    firmware_name: str | None = None
    # Kernel clocks in Hz (`sysclk`, `pclk1`, `pclk2`, `timer`, `spi.<n>`, `uart.<usartN|lpuartN>`).
    clocks: dict[str, Any] = field(default_factory=dict)
    known_gaps: tuple[KnownGap, ...] = ()

    def matches_firmware_name(self, reported: str) -> bool:
        """`info`/`EVT boot` report e.g. `NUCLEO-WB55RG` for the `nucleo_wb55rg` board file."""
        expected = self.firmware_name or self.name
        return reported.lower().replace("-", "_") == expected.lower().replace("-", "_")

    def resolve_pin(self, pin: str) -> str:
        return normalize_pin(pin, self.pins)

    def clock(self, kind: str, instance: int | str | None = None) -> int:
        """`clock("timer")`, `clock("spi", 1)`, `clock("uart", "lpuart1")`: a kernel clock of the `clocks` section."""
        node: Any = self.clocks.get(kind)
        if instance is not None and isinstance(node, Mapping):
            key = instance.lower() if isinstance(instance, str) else instance
            node = node.get(key)
        if isinstance(node, bool) or not isinstance(node, int):
            suffix = "" if instance is None else f".{instance}"
            raise ConfigError(f"{self.name}: clocks.{kind}{suffix} is not configured")
        return node

    def param(self, path: str, default: Any = _MISSING) -> Any:
        """`param("pwm.frequencies")` → `tests.pwm.frequencies`."""
        node: Any = self.tests
        for part in path.split("."):
            if isinstance(node, Mapping) and part in node:
                node = node[part]
            elif default is not _MISSING:
                return default
            else:
                raise ConfigError(f"{self.name}: tests.{path} is not configured")
        return node

    def matrix(self, path: str) -> dict[str, list[Any]]:
        """`tests.<path>` as an ordered mapping of dimension name to a non-empty list of values."""
        node = self.param(path)
        if not isinstance(node, Mapping) or not node:
            raise ConfigError(f"{self.name}: tests.{path} must be a mapping of dimension to values")
        result: dict[str, list[Any]] = {}
        for name, values in node.items():
            values = list(values) if isinstance(values, (list, tuple)) else [values]
            if not values:
                raise ConfigError(f"{self.name}: tests.{path}.{name} has no values")
            result[str(name)] = values
        return result

    def aliases_of(self, pin: str) -> list[str]:
        """Aliases of the board table that name `pin`."""
        resolved = self.resolve_pin(pin)
        return [alias for alias, target in self.pins.items() if target == resolved]

    def set_param(self, path: str, value: Any) -> None:
        parts = path.split(".")
        node = self.tests
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = value

    def apply_overrides(self, overrides: Iterable[str]) -> None:
        """`pwm.frequencies=[20000]` style overrides (values parsed as YAML)."""
        for override in overrides:
            path, sep, text = override.partition("=")
            if not sep:
                raise ConfigError(f"override must be path=value: {override!r}")
            self.set_param(path.strip(), yaml.safe_load(text))

    def wiring(self, names: Iterable[str], enabled: Iterable[str] = ()) -> Wiring:
        """The wiring of the sets `names` with the options `enabled`; `ConfigError` for an unknown set, for sets
        that wire one AD3 channel differently, for an enabled option no selected set offers (or enabled options
        without a set) and for enabled options that exclude each other."""
        tags = frozenset(enabled)
        selected: list[Connection] = []
        jumpers: list[str] = []
        options: dict[str, WiringOption] = {}
        used: dict[tuple[str, int], str] = {}
        names = tuple(name for name in names if name)
        for name in names:
            if name not in self.wiring_sets:
                known = ", ".join(sorted(self.wiring_sets))
                raise ConfigError(f"{self.name}: unknown wiring set {name!r} (known: {known})")
            wiring_set = self.wiring_sets[name]
            jumpers += [jumper for jumper in wiring_set.jumpers if jumper not in jumpers]
            for tag, option in wiring_set.options.items():
                options[tag] = options[tag].merged(option) if tag in options else option
            for connection in wiring_set.connections:
                if connection.requires and connection.requires not in tags:
                    continue
                key = (connection.kind, connection.channel)
                if key in used and used[key] != name:
                    position, other = next((i, c) for i, c in enumerate(selected) if (c.kind, c.channel) == key)
                    if other.pins != connection.pins:
                        raise ConfigError(f"{connection.kind}{connection.channel} is wired differently in sets {used[key]!r} and {name!r}")
                    if other.role is None and connection.role is not None:
                        selected[position] = replace(other, role=connection.role, note=other.note or connection.note)
                    continue
                used[key] = name
                selected.append(connection)
        self._check_options(names, options, tags)
        option_jumpers = {tag: options[tag].jumpered for tag in sorted(tags) if options[tag].jumpered}
        return Wiring(names, tuple(selected), tags, tuple(jumpers), options, option_jumpers)

    def _check_options(self, names: tuple[str, ...], options: Mapping[str, WiringOption], tags: frozenset[str]) -> None:
        if tags and not names:
            raise ConfigError(f"{self.name}: --with {', '.join(sorted(tags))} needs the --wiring-set that offers it")
        missing = sorted(tags - set(options))
        if missing:
            offered = ", ".join(sorted(options)) or "none"
            raise ConfigError(f"{self.name}: wiring set(s) {', '.join(names)} offer no --with {', '.join(missing)} (offered: {offered})")
        for tag in sorted(tags):
            clash = sorted(options[tag].excludes & tags)
            if clash:
                raise ConfigError(f"{self.name}: --with {tag} and --with {', '.join(clash)} exclude each other: fit one of them")


def _connection(kind: ChannelKind, channel: Any, spec: Any, pins: Mapping[str, str]) -> Connection:
    if isinstance(spec, str) or spec is None:
        spec = {"pin": spec}
    if not isinstance(spec, Mapping):
        raise ConfigError(f"bad {kind}{channel} entry: {spec!r}")
    pin = spec.get("pin")
    jumpered = spec.get("jumpered") or ()
    if isinstance(jumpered, str) or not isinstance(jumpered, (list, tuple)):
        raise ConfigError(f"{kind}{channel}: jumpered must be a list of pins")
    return Connection(
        kind=kind,
        channel=int(channel),
        pin=None if pin is None else normalize_pin(str(pin), pins, strict=True),
        role=spec.get("role"),
        note=str(spec.get("note", "")),
        requires=spec.get("requires"),
        jumpered=tuple(normalize_pin(str(other), pins, strict=True) for other in jumpered),
    )


def _pin_list(where: str, value: Any, pins: Mapping[str, str]) -> tuple[str, ...]:
    if value is None:
        return ()
    if isinstance(value, str) or not isinstance(value, (list, tuple)):
        raise ConfigError(f"{where} must be a list of pins")
    return tuple(normalize_pin(str(pin), pins, strict=True) for pin in value)


def _option(set_name: str, tag: str, spec: Any, pins: Mapping[str, str]) -> WiringOption:
    """An option is a description (the wiring is only described) or a mapping of `_OPTION_KEYS`."""
    if spec is None or isinstance(spec, str):
        return WiringOption(tag, str(spec or ""))
    where = f"{set_name}: option {tag}"
    if not isinstance(spec, Mapping):
        raise ConfigError(f"{where} must be a description or a mapping")
    unknown = sorted(str(key) for key in set(spec) - _OPTION_KEYS)
    if unknown:
        raise ConfigError(f"{where}: unknown keys {', '.join(unknown)} (known: {', '.join(sorted(_OPTION_KEYS))})")
    jumpered_raw = spec.get("jumpered") or {}
    if not isinstance(jumpered_raw, Mapping):
        raise ConfigError(f"{where}: jumpered must map a pin to the pins it is tied to")
    jumpered = {
        normalize_pin(str(key), pins, strict=True): _pin_list(f"{where}: jumpered.{key}", others, pins)
        for key, others in jumpered_raw.items()
    }
    excludes = spec.get("excludes") or []
    if isinstance(excludes, str) or not isinstance(excludes, (list, tuple)):
        raise ConfigError(f"{where}: excludes must be a list of option tags")
    return WiringOption(
        tag=tag,
        description=str(spec.get("description", "")),
        jumpered=jumpered,
        loads=_pin_list(f"{where}: loads", spec.get("loads"), pins),
        pullups=_pin_list(f"{where}: pullups", spec.get("pullups"), pins),
        excludes=frozenset(str(other) for other in excludes),
    )


def _wiring_set(name: str, raw: Mapping[str, Any], pins: Mapping[str, str]) -> WiringSet:
    connections: list[Connection] = []
    for kind in _KINDS:
        for channel, spec in (raw.get(kind) or {}).items():
            connections.append(_connection(kind, channel, spec, pins))
    options = {str(tag): _option(name, str(tag), spec, pins) for tag, spec in (raw.get("options") or {}).items()}
    for connection in connections:
        if connection.kind == "dio" and not 0 <= connection.channel <= 15:
            raise ConfigError(f"{name}: DIO{connection.channel} does not exist on the AD3")
        if connection.kind != "dio" and connection.channel not in (1, 2):
            raise ConfigError(f"{name}: {connection.kind} channel must be 1 or 2")
        if connection.requires is not None and connection.requires not in options:
            what = f"{connection.kind}{connection.channel}"
            raise ConfigError(f"{name}: {what} requires option {connection.requires!r}, which the set does not offer")
    return WiringSet(
        name=name,
        description=str(raw.get("description", "")),
        connections=tuple(connections),
        options=options,
        jumpers=tuple(raw.get("jumpers") or ()),
    )


def _clocks(raw: Mapping[str, Any]) -> dict[str, Any]:
    """Clock frequencies as ints; per-instance tables keep int keys (`spi: {1: ...}`) or lower-case names
    (`uart: {lpuart1: ...}`)."""
    result: dict[str, Any] = {}
    for kind, value in raw.items():
        if isinstance(value, Mapping):
            result[str(kind)] = {(key.lower() if isinstance(key, str) else int(key)): int(hz) for key, hz in value.items()}
        else:
            result[str(kind)] = int(value)
    return result


def _known_gaps(raw: Any) -> tuple[KnownGap, ...]:
    if not isinstance(raw, list):
        raise ConfigError("known_gaps must be a list")
    gaps = []
    for entry in raw:
        if not isinstance(entry, Mapping) or set(entry) - {"tests", "reason", "hangs"}:
            raise ConfigError(f"known_gaps entries take tests, reason and hangs: {entry!r}")
        tests, reason, hangs = entry.get("tests"), entry.get("reason"), entry.get("hangs", False)
        if not isinstance(tests, list) or not tests or not all(isinstance(test, str) and "::" in test for test in tests):
            raise ConfigError(f"known_gaps tests must be <module>::<test> patterns: {entry!r}")
        if not isinstance(reason, str) or not reason.strip() or not isinstance(hangs, bool):
            raise ConfigError(f"known_gaps entry needs a reason and a boolean hangs: {entry!r}")
        gaps.append(KnownGap(tuple(tests), reason.strip(), hangs))
    return tuple(gaps)


def parse_board(raw: Mapping[str, Any], path: Path | None = None, extras: Iterable[Path] = ()) -> BoardConfig:
    try:
        pins = {str(alias).lower(): normalize_pin(str(pin)) for alias, pin in (raw.get("pins") or {}).items()}
        unknown = sorted(alias for alias in pins if not is_alias(alias))
        if unknown:
            raise ConfigError(f"aliases outside the generic naming scheme: {', '.join(unknown)}")
        terminal_raw = dict(raw.get("terminal") or {})
        terminal_raw["pins"] = tuple(normalize_pin(str(pin), pins, strict=True) for pin in terminal_raw.get("pins", ()))
        ad3_raw = dict(raw.get("ad3") or {})
        limits = ad3_raw.pop("analog_limits", None)
        if limits is not None:
            ad3_raw["analog_min"], ad3_raw["analog_max"] = (float(value) for value in limits)
        return BoardConfig(
            name=str(raw["board"]),
            family=str(raw["family"]),
            description=str(raw.get("description", "")),
            sysclk=raw.get("sysclk"),
            terminal=TerminalConfig(**terminal_raw),
            pins=pins,
            ad3=Ad3Config(**ad3_raw),
            wiring_sets={name: _wiring_set(name, spec or {}, pins) for name, spec in (raw.get("wiring_sets") or {}).items()},
            tests=copy.deepcopy(dict(raw.get("tests") or {})),
            path=path,
            extras=tuple(extras),
            firmware_name=raw.get("firmware_name"),
            clocks=_clocks(raw.get("clocks") or {}),
            known_gaps=_known_gaps(raw.get("known_gaps") or []),
        )
    except (KeyError, TypeError, ValueError) as error:
        raise ConfigError(f"{path or 'board'}: {error}") from error


def board_path(name_or_path: str | Path, boards_dir: Path = BOARDS_DIR) -> Path:
    candidate = Path(name_or_path)
    if candidate.suffix in (".yaml", ".yml") and candidate.exists():
        return candidate
    named = boards_dir / f"{name_or_path}.yaml"
    if named.exists():
        return named
    known = ", ".join(sorted(path.stem for path in boards_dir.glob("*.yaml")))
    raise ConfigError(f"no board {name_or_path!r} (known: {known})")


def merge_board(base: Any, extra: Any) -> Any:
    """`extra` deep-merged over `base`: mappings merge key by key, lists append, a scalar or a change of type
    replaces, and a key ending in `!` replaces the value of the key without it (`limit_pins!: [...]`)."""
    if isinstance(base, Mapping) and isinstance(extra, Mapping):
        result = dict(base)
        for key, value in extra.items():
            if isinstance(key, str) and key.endswith("!"):
                result[key[:-1]] = _without_bangs(value)
            elif key in result:
                result[key] = merge_board(result[key], value)
            else:
                result[key] = _without_bangs(value)
        return result
    if isinstance(base, list) and isinstance(extra, list):
        return base + _without_bangs(extra)
    return _without_bangs(extra)


def _without_bangs(value: Any) -> Any:
    """A copy of `value` whose `key!` mapping keys are plain keys (nothing to replace inside a new value)."""
    if isinstance(value, Mapping):
        return {(key[:-1] if isinstance(key, str) and key.endswith("!") else key): _without_bangs(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_without_bangs(item) for item in value]
    return copy.deepcopy(value)


def _read_yaml(path: Path) -> Any:
    try:
        with path.open(encoding="utf-8") as stream:
            return yaml.safe_load(stream)
    except (OSError, yaml.YAMLError) as error:
        raise ConfigError(f"{path}: {error}") from error


def load_board(name_or_path: str | Path, extras: Iterable[str | Path] = (), boards_dir: Path = BOARDS_DIR) -> BoardConfig:
    """The board file, with the `extras` (`--board-extra` files) deep-merged over it in order (`merge_board`)."""
    path = board_path(name_or_path, boards_dir)
    raw = _read_yaml(path)
    extra_paths = tuple(Path(extra) for extra in extras)
    for extra_path in extra_paths:
        if not extra_path.is_file():
            raise ConfigError(f"no board extra file {extra_path}")
        extra = _read_yaml(extra_path)
        if extra is None:
            continue
        if not isinstance(extra, Mapping):
            raise ConfigError(f"{extra_path}: a board extra file must be a mapping")
        raw = merge_board(raw, extra)
    return parse_board(raw, path, extra_paths)


def available_boards(boards_dir: Path = BOARDS_DIR) -> list[str]:
    return sorted(path.stem for path in boards_dir.glob("*.yaml"))
