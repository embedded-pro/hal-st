"""Building blocks of the fake firmware's command groups: the argument helpers that mirror EMIL's `HilArguments`
(`ERR usage` / `ERR range` as `FakeError`) and `FakeGroup`, the base of the groups in `fakes/*.py`.

A group sees the firmware through `self.fw` (a `FakeFirmware`): `spec` (the board profile), `pin()`, `bonded()`,
`supports()`, `check_function()`, `first_function_pin()`, `supports_analog()`, `reserved()`, `timer_exists()`,
`check_pins()`/`claim()`/`release()` (the `HilPinPool`), `timer_owners` (`TimerAllocation`),
`check_resources()`/`claim_resource()`/`release_resources()` (`ResourceAllocation`: lpTimer, spi, i2c, adc, dma1,
dma2, hsem), `open_instance()`/`find_instance()`/`close_instance()` (one instance per group, with its pins, timer
and resources), `group()`, `event()`, `clock()` and `sleep()`. Owners are `(prefix, key)` tuples, as the
firmware's groups use one owner id each.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping
from typing import TYPE_CHECKING, ClassVar

if TYPE_CHECKING:
    from ..fake_firmware import FakeFirmware

__all__ = ["Command", "FakeError", "FakeGroup", "_Error", "_choice", "_fail", "_flag", "_hex", "_number", "_shape", "_tokens"]

UINT32_MAX = 0xFFFFFFFF

Command = Callable[[list[str], dict[str, str]], str | list[str] | None]


class FakeError(Exception):
    """Answered as `ERR <reason>`."""

    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


_Error = FakeError


def _fail(reason: str) -> None:
    raise FakeError(reason)


def _parse_uint(text: str) -> int:
    """`HilArguments::ParseNumber`: decimal or `0x` hex, unsigned 32 bits."""
    base, digits = 10, "0123456789"
    if len(text) > 2 and text[0] == "0" and text[1] in "xX":
        base, digits, text = 16, "0123456789abcdefABCDEF", text[2:]
    if not text or any(char not in digits for char in text):
        _fail("usage")
    value = int(text, base)
    if value > UINT32_MAX:
        _fail("usage")
    return value


def _number(text: str | None, low: int = 0, high: int = UINT32_MAX) -> int:
    if text is None:
        _fail("usage")
    assert text is not None
    value = _parse_uint(text)
    if not low <= value <= high:
        _fail("range")
    return value


def _flag(options: Mapping[str, str], key: str, default: bool = False) -> bool:
    if key not in options:
        return default
    return _number(options[key], 0, 1) == 1


def _choice(options: Mapping[str, str], key: str, choices: Iterable[str], default: str) -> str:
    value = options.get(key, default)
    if value not in tuple(choices):
        _fail("usage")
    return value


def _shape(args: list[str], options: Mapping[str, str], low: int, high: int, keys: Iterable[str] = ()) -> None:
    """`HilArguments::Shape`: positional count and known keys, else `ERR usage`."""
    allowed = tuple(keys)
    if not low <= len(args) <= high or any(key not in allowed for key in options):
        _fail("usage")


def _tokens(text: str) -> list[str]:
    """`infra::Tokenizer` on `,`: empty entries are skipped."""
    return [token for token in text.split(",") if token]


def _hex(text: str, capacity: int) -> bytes:
    """`HilArguments::ParseHex`: `-` is empty, odd or non-hex is `usage`, beyond `capacity` is `range`."""
    if text == "-":
        return b""
    if not text or len(text) % 2:
        _fail("usage")
    if len(text) // 2 > capacity:
        _fail("range")
    try:
        return bytes.fromhex(text)
    except ValueError:
        raise FakeError("usage") from None


class FakeGroup:
    """One command group of the fake firmware, discovered in `fakes/*.py` (every subclass with a `prefix`).

    `<prefix>.<verb> args key=value` runs `cmd_<verb>(args, options)`, which returns the final line(s) (or None
    for a command without one) and raises `FakeError` for `ERR <reason>`. Check arguments in the firmware's order
    (`usage`, `range`, `pin`, `unsupported`, then `busy`). `boot()` runs on every boot (state is lost), `poll()`
    whenever the host reads or writes (time-driven output)."""

    prefix: ClassVar[str] = ""

    def __init__(self, fw: FakeFirmware) -> None:
        self.fw = fw

    @property
    def firmware(self) -> FakeFirmware:
        return self.fw

    def boot(self) -> None:
        """The firmware rebooted: the pins, timers and resources the group held are already released."""

    def poll(self) -> None:
        """Called whenever the host reads or writes; print `EVT` lines with `self.fw.event()`."""

    def handler(self, verb: str) -> Command | None:
        method = getattr(self, "cmd_" + verb, None)
        return method if callable(method) else None
