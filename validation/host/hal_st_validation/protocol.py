"""hal-st specifics of the validation terminal protocol (see validation/PROTOCOL.md): error reasons, pins and
aliases. Framing, parsing and formatting come from `ad3_waveforms_bench.protocol`.

The alias table of a board comes from its board file (`pins`, compared with the firmware's `board.pins`);
`PIN_ALIASES` is the generic naming scheme those tables use, so a known alias can be passed through to the
firmware unresolved when no table is at hand.
"""

from __future__ import annotations

import re
from collections.abc import Mapping

from ad3_waveforms_bench.protocol import ProtocolError, Response

ERROR_REASONS = frozenset({"usage", "pin", "busy", "notopen", "unsupported", "range", "timeout", "failed"})

_ALIAS_PATTERNS = (
    r"terminal(tx|rx)",
    r"ain(1[0-9]|[0-9])",
    r"tim(1[0-7]|[1-9])ch[1-4]",
    r"tim(1[0-7]|[1-9])ch[1-3]n",
    r"tim(1[0-7]|[1-9])bkin",
    r"qei(1[0-7]|[1-9])(a|b|idx)",
    r"lptim[12]in[12]",
    r"spi[1-3](clk|miso|mosi|cs)",
    r"usart[1-3](tx|rx|rts|cts)",
    r"lpuart1(tx|rx|rts|cts)",
    r"led[0-7]",
    r"gpio(1[0-5]|[0-9])",
    r"sw[1-3]",
)
_ALIAS_RE = re.compile("^(?:" + "|".join(_ALIAS_PATTERNS) + ")$")


def _generic_aliases() -> frozenset[str]:
    names = {f"terminal{signal}" for signal in ("tx", "rx")}
    names |= {f"ain{channel}" for channel in range(20)}
    for timer in range(1, 18):
        names |= {f"tim{timer}ch{channel}" for channel in range(1, 5)}
        names |= {f"tim{timer}ch{channel}n" for channel in range(1, 4)}
        names |= {f"tim{timer}bkin"}
        names |= {f"qei{timer}{signal}" for signal in ("a", "b", "idx")}
    names |= {f"lptim{index}in{line}" for index in (1, 2) for line in (1, 2)}
    names |= {f"spi{index}{signal}" for index in range(1, 4) for signal in ("clk", "miso", "mosi", "cs")}
    names |= {f"usart{index}{signal}" for index in range(1, 4) for signal in ("tx", "rx", "rts", "cts")}
    names |= {f"lpuart1{signal}" for signal in ("tx", "rx", "rts", "cts")}
    names |= {f"led{index}" for index in range(8)}
    names |= {f"gpio{index}" for index in range(16)}
    names |= {f"sw{index}" for index in range(1, 4)}
    assert all(_ALIAS_RE.match(name) for name in names)
    return frozenset(names)


PIN_ALIASES = _generic_aliases()

# STM32 ports A-K, index 0-15 without leading zero (EMIL's `HilPinNamingDefault` refuses `PA05`).
_PIN_RE = re.compile(r"^P([A-K])(1[0-5]|[0-9])$", re.IGNORECASE)


def is_pin(text: str) -> bool:
    return bool(_PIN_RE.match(text))


def is_alias(text: str) -> bool:
    return text.lower() in PIN_ALIASES


def pin_parts(pin: str) -> tuple[str, int]:
    """(`port letter`, `index`) of a `P<port><index>` pin."""
    match = _PIN_RE.match(pin)
    if match is None:
        raise ProtocolError(f"not a pin: {pin!r}")
    return match.group(1).upper(), int(match.group(2))


def normalize_pin(pin: str, aliases: Mapping[str, str] | None = None, strict: bool = False) -> str:
    """Canonical `P<port><index>` for a pin or an alias of `aliases`.

    A generic alias that `aliases` does not resolve is returned lower case (the firmware resolves it), unless
    `strict` requires it to be in `aliases`.
    """
    match = _PIN_RE.match(pin)
    if match:
        return f"P{match.group(1).upper()}{int(match.group(2))}"
    name = pin.lower()
    if aliases is not None and name in aliases:
        return normalize_pin(aliases[name])
    if not strict and name in PIN_ALIASES:
        return name
    raise ProtocolError(f"not a pin or alias: {pin!r}")


def parse_pin_map(response: Response) -> dict[str, str]:
    """`board.pins` → `OK alias=pin,alias=pin,...` (space separated pairs are accepted too)."""
    body = response.raw.split(maxsplit=1)[1] if " " in response.raw else ""
    result: dict[str, str] = {}
    for item in re.split(r"[\s,]+", body):
        key, sep, value = item.partition("=")
        if sep and key:
            result[key.lower()] = value.upper()
    return result
