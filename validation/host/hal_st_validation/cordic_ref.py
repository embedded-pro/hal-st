"""Reference model of the STM32G474 CORDIC behind `cordic.calc` (validation/PROTOCOL.md, `hal::CordicBaseStm`).

Coding: q1.31 words (`insize=32`/`outsize=32`) or two q1.15 halves per word (`16`: argument or result 1 in the low
half, 2 in the high half), little-endian on the wire. With q1.31 a calculation takes `nargs` words and gives `nres`
words; a q1.31 calculation with `nargs=1` uses ARG2 = +1 (0x7FFFFFFF), its value after the reset of the driver.

Functions in the RM0440 conventions: angles in units of pi; `atan`, `ln` and `sqrt` take x * 2^-n with n the
`scale`, the hyperbolic functions x * 2^-1 (scale 1); `atan` gives atan(x) * 2^-n / pi, `ln` ln(x) * 2^-(n+1),
`sqrt` sqrt(x) * 2^-n, the hyperbolic ones their value * 2^-1. `DOMAINS` is the argument range (in the scaled
units written to the CORDIC) per function and scale.

The error bound for a precision of P cycles (4 iterations each) is iteration-limited, 2^-(4P - slack), with a floor
for the word length; slack and floor come from the board file (`tests.cordic.tolerance`). RM0440 was not at hand
when this was written: the domains and the bound are to be confirmed, and tightened, from a bench run.
"""

from __future__ import annotations

import math
import struct
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

Function = Literal["cos", "sin", "phase", "mod", "atan", "cosh", "sinh", "atanh", "ln", "sqrt"]
Width = Literal[32, 16]

FUNCTIONS: tuple[Function, ...] = ("cos", "sin", "phase", "mod", "atan", "cosh", "sinh", "atanh", "ln", "sqrt")
TWO_ARGUMENTS: frozenset[str] = frozenset({"cos", "sin", "phase", "mod"})
TWO_RESULTS: frozenset[str] = frozenset({"cos", "sin", "phase", "mod", "cosh", "sinh"})
PRECISIONS = range(1, 16)
WORD_BYTES = 4

# Argument ranges (scaled units) per function and scale; the second range is ARG2 where the function takes one.
DOMAINS: dict[str, dict[int, tuple[tuple[float, float], ...]]] = {
    "cos": {0: ((-1.0, 0.999), (0.0, 1.0))},
    "sin": {0: ((-1.0, 0.999), (0.0, 1.0))},
    "phase": {0: ((-0.7, 0.7), (-0.7, 0.7))},
    "mod": {0: ((-0.7, 0.7), (-0.7, 0.7))},
    "atan": {scale: ((-1.0, 0.999),) for scale in range(8)},
    "cosh": {1: ((-0.559, 0.559),)},
    "sinh": {1: ((-0.559, 0.559),)},
    "atanh": {1: ((-0.403, 0.403),)},
    "ln": {1: ((0.0535, 0.5),), 2: ((0.25, 0.75),), 3: ((0.375, 0.875),), 4: ((0.4375, 0.584),)},
    "sqrt": {0: ((0.027, 0.75),), 1: ((0.375, 0.875),), 2: ((0.4375, 0.585),)},
}

_LSB = {32: 2.0**-31, 16: 2.0**-15}


def scales(function: Function) -> tuple[int, ...]:
    return tuple(DOMAINS[function])


def default_scale(function: Function) -> int:
    return scales(function)[0]


def lsb(width: Width) -> float:
    return _LSB[width]


def encode(value: float, width: Width) -> int:
    """Signed q1.31 or q1.15 code of `value`, rounded to nearest and saturated."""
    bits = width - 1
    code = round(value * (1 << bits))
    return max(-(1 << bits), min((1 << bits) - 1, code))


def decode(code: int, width: Width) -> float:
    """Value of a signed code; `code` may be unsigned (it is sign-extended from `width` bits)."""
    code &= (1 << width) - 1
    if code >= 1 << (width - 1):
        code -= 1 << width
    return code / (1 << (width - 1))


def saturate(value: float) -> float:
    return max(-1.0, min(1.0 - 2.0**-31, value))


def reference(function: Function, scale: int, arg1: float, arg2: float = 1.0) -> tuple[float, float]:
    """(RES1, RES2) of one calculation, unsaturated except where the function is undefined (out of its domain)."""
    if function in ("cos", "sin"):
        angle = arg1 * math.pi
        cosine, sine = arg2 * math.cos(angle), arg2 * math.sin(angle)
        return (cosine, sine) if function == "cos" else (sine, cosine)
    if function in ("phase", "mod"):
        phase, modulus = math.atan2(arg2, arg1) / math.pi, math.hypot(arg1, arg2)
        return (phase, modulus) if function == "phase" else (modulus, phase)
    x = arg1 * 2.0**scale
    if function == "atan":
        return math.atan(x) * 2.0**-scale / math.pi, 0.0
    if function in ("cosh", "sinh"):
        cosh, sinh = math.cosh(min(abs(x), 20.0)) / 2, math.copysign(math.sinh(min(abs(x), 20.0)), x) / 2
        return (cosh, sinh) if function == "cosh" else (sinh, cosh)
    if function == "atanh":
        if abs(x) >= 1:
            return math.copysign(1.0, x), 0.0
        return math.atanh(x) / 2, 0.0
    if function == "ln":
        if x <= 0:
            return -1.0, 0.0
        return math.log(x) * 2.0 ** -(scale + 1), 0.0
    if function == "sqrt":
        return (math.sqrt(x) * 2.0**-scale if x > 0 else 0.0), 0.0
    raise ValueError(f"unknown function {function!r}")


def argument_words(insize: Width, nargs: int) -> int:
    return 1 if insize == 16 else nargs


def result_words(outsize: Width, nres: int) -> int:
    return 1 if outsize == 16 else nres


def pack_arguments(arguments: Sequence[tuple[float, float]], insize: Width = 32, nargs: int = 1) -> bytes:
    """The `cordic.calc` payload for (ARG1, ARG2) pairs; ARG2 is dropped for q1.31 with `nargs=1`."""
    words: list[int] = []
    for arg1, arg2 in arguments:
        if insize == 16:
            words.append((encode(arg2, 16) & 0xFFFF) << 16 | (encode(arg1, 16) & 0xFFFF))
        else:
            words.extend(code & 0xFFFFFFFF for code in (encode(arg1, 32), encode(arg2, 32))[:nargs])
    return struct.pack(f"<{len(words)}I", *words)


def unpack_arguments(data: bytes, insize: Width = 32, nargs: int = 1) -> list[tuple[float, float]]:
    """Inverse of `pack_arguments` (ARG2 = +1 where the payload has none)."""
    words = struct.unpack(f"<{len(data) // WORD_BYTES}I", data[: len(data) // WORD_BYTES * WORD_BYTES])
    if insize == 16:
        return [(decode(word & 0xFFFF, 16), decode(word >> 16, 16)) for word in words]
    if nargs == 1:
        return [(decode(word, 32), decode(0x7FFFFFFF, 32)) for word in words]
    return [(decode(words[i], 32), decode(words[i + 1], 32)) for i in range(0, len(words) - 1, 2)]


def unpack_results(data: bytes, outsize: Width = 32, nres: int = 1) -> list[tuple[float, ...]]:
    """The results of a `cordic.calc` reply, one tuple (RES1[, RES2]) per calculation."""
    words = struct.unpack(f"<{len(data) // WORD_BYTES}I", data)
    if outsize == 16:
        return [(decode(word & 0xFFFF, 16), decode(word >> 16, 16)) for word in words]
    return [tuple(decode(word, 32) for word in words[i : i + nres]) for i in range(0, len(words), nres)]


def pack_results(results: Sequence[tuple[float, float]], outsize: Width = 32, nres: int = 1) -> bytes:
    words: list[int] = []
    for res1, res2 in results:
        if outsize == 16:
            words.append((encode(res2, 16) & 0xFFFF) << 16 | (encode(res1, 16) & 0xFFFF))
        else:
            words.extend(code & 0xFFFFFFFF for code in (encode(res1, 32), encode(res2, 32))[:nres])
    return struct.pack(f"<{len(words)}I", *words)


@dataclass(frozen=True)
class Tolerance:
    """`tests.cordic.tolerance`: the bound is max(2^-(4P - slack_bits - extra), floor_lsb LSB of the output)."""

    slack_bits: int = 2
    floor_q31: int = 8192
    floor_q15: int = 2
    # Extra slack for the functions that converge slower than one bit per iteration.
    extra_bits: dict[str, int] | None = None

    def bound(self, function: Function, precision: int, outsize: Width) -> float:
        extra = (self.extra_bits or {}).get(function, 0)
        floor = (self.floor_q15 if outsize == 16 else self.floor_q31) * lsb(outsize)
        return max(2.0 ** -(4 * precision - self.slack_bits - extra), floor)


def errors(
    function: Function,
    scale: int,
    arguments: Sequence[tuple[float, float]],
    results: Sequence[tuple[float, ...]],
) -> list[float]:
    """The absolute error of every result against the saturated reference of the arguments actually sent."""
    found: list[float] = []
    for (arg1, arg2), result in zip(arguments, results, strict=True):
        expected = reference(function, scale, arg1, arg2)
        found.extend(abs(value - saturate(ideal)) for value, ideal in zip(result, expected, strict=False))
    return found


def simulate(function: Function, scale: int, precision: int, data: bytes, insize: Width, outsize: Width, nargs: int, nres: int) -> bytes:
    """The fake firmware's answer: the reference of the decoded arguments plus an error of 2^-(4P + 1) of alternating
    sign, so that a lower precision is measurably less accurate, as on the CORDIC."""
    arguments = unpack_arguments(data, insize, nargs)
    offset = 2.0 ** -(4 * precision + 1)
    results: list[tuple[float, float]] = []
    for index, (arg1, arg2) in enumerate(arguments):
        res1, res2 = reference(function, scale, arg1, arg2)
        sign = 1.0 if index % 2 == 0 else -1.0
        results.append((saturate(res1 + sign * offset), saturate(res2 - sign * offset)))
    return pack_results(results, outsize, nres)


def sweep(function: Function, scale: int, count: int) -> list[tuple[float, float]]:
    """`count` arguments spread evenly over the domain (ARG2 over its own range, in the opposite direction)."""
    ranges = DOMAINS[function][scale]
    (low1, high1), (low2, high2) = ranges[0], ranges[1] if len(ranges) > 1 else (1.0, 1.0)
    if count == 1:
        return [((low1 + high1) / 2, (low2 + high2) / 2)]
    step = 1.0 / (count - 1)
    return [(low1 + (high1 - low1) * i * step, high2 - (high2 - low2) * i * step) for i in range(count)]
