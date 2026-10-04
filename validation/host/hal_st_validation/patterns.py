"""Host twin of the firmware-generated payloads (`len=`, `pattern=`, `seed=` of validation/PROTOCOL.md; firmware
`validation/firmware/Payload.cpp`) and of their `crc=` replies.

`inc`: byte i = (seed + i) & 0xFF; `const`: seed & 0xFF; `prbs`: xorshift32 (x ^= x << 13; x ^= x >> 17;
x ^= x << 5, on 32 bits) from state `seed or 1`, one step per byte, the low byte of the state.
"""

from __future__ import annotations

import zlib
from typing import Literal

Pattern = Literal["inc", "const", "prbs"]
PATTERNS: tuple[Pattern, ...] = ("inc", "const", "prbs")
SEED_MAX = 0xFFFFFFFF
_MASK32 = 0xFFFFFFFF

# `infra::Crc32` of the firmware (reflected, polynomial 0xEDB88320, initial and final xor 0xFFFFFFFF).
crc32 = zlib.crc32


def generate(length: int, pattern: Pattern = "inc", seed: int = 0) -> bytes:
    if length < 0:
        raise ValueError(f"length must not be negative: {length}")
    if not 0 <= seed <= SEED_MAX:
        raise ValueError(f"seed must fit 32 bits: {seed}")
    if pattern == "inc":
        return bytes((seed + index) & 0xFF for index in range(length))
    if pattern == "const":
        return bytes([seed & 0xFF]) * length
    if pattern == "prbs":
        state = seed or 1
        result = bytearray(length)
        for index in range(length):
            state ^= (state << 13) & _MASK32
            state ^= state >> 17
            state ^= (state << 5) & _MASK32
            result[index] = state & 0xFF
        return bytes(result)
    raise ValueError(f"unknown pattern {pattern!r}")


def crc_text(data: bytes) -> str:
    """The `crc=` field of a reply: CRC-32 as 8 lower-case hex digits."""
    return f"{crc32(data):08x}"
