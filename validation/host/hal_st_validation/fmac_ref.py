"""Bit-exact reference model of the STM32G474 FMAC behind `fmac.fir` and `fmac.iir` (validation/PROTOCOL.md,
`hal::FmacBaseStm`).

Samples and coefficients are q1.15 (int16). A FIR with P taps gives y[n] = 2^R * sum(b[k] * x[n - k], k < P); a
direct-form-1 IIR adds sum(a[k] * y[n - k], 1 <= k <= Q) (the FMAC adds the feedback: negate the a of the textbook
form). The driver preloads P - 1 input samples (`hist`, oldest first, zero-padded on the old side) and, for an IIR,
Q output samples (`yhist`), so every input sample gives one output sample.

The arithmetic: each q2.30 product is truncated to the accumulator's `ACC_FRAC` fractional bits, the sum wraps in
`ACC_BITS` bits, the gain shifts it left by R, and the output takes the q1.15 bits by an arithmetic right shift
(truncation), saturated with `clip=1` or wrapped to 16 bits without. `sat` is set when an output did not fit q1.15.
RM0440 was not at hand when this was written: `Arithmetic` holds these choices as parameters, the calibration
vectors of tests/unit/test_fmac_ref.py tell the alternatives apart, and the first bench run decides them.
"""

from __future__ import annotations

import struct
import zlib
from collections.abc import Sequence
from dataclasses import dataclass

Q15_MIN = -0x8000
Q15_MAX = 0x7FFF
FIR_TAPS = range(2, 128)
IIR_P = range(2, 65)
IIR_Q = range(1, 64)
GAINS = range(8)


@dataclass(frozen=True)
class Arithmetic:
    acc_bits: int = 26
    acc_frac: int = 22
    # "floor" truncates every product to the accumulator LSB, "exact" keeps all 30 fractional bits.
    product: str = "floor"
    # Sign of the IIR feedback term.
    feedback: int = 1

    def product_of(self, coefficient: int, sample: int) -> int:
        full = coefficient * sample
        if self.product == "exact":
            return full
        return full >> (30 - self.acc_frac)

    def frac(self) -> int:
        return 30 if self.product == "exact" else self.acc_frac

    def wrap_accumulator(self, value: int) -> int:
        bits = self.acc_bits + (30 - self.acc_frac if self.product == "exact" else 0)
        value &= (1 << bits) - 1
        return value - (1 << bits) if value >> (bits - 1) else value

    def output(self, accumulator: int, gain: int, clip: bool) -> tuple[int, bool]:
        value = (accumulator << gain) >> (self.frac() - 15)
        if Q15_MIN <= value <= Q15_MAX:
            return value, False
        if clip:
            return (Q15_MAX if value > 0 else Q15_MIN), True
        value &= 0xFFFF
        return (value - 0x10000 if value & 0x8000 else value), True


DEFAULT = Arithmetic()


def to_q15(value: float) -> int:
    return max(Q15_MIN, min(Q15_MAX, round(value * 0x8000)))


def pack(samples: Sequence[int]) -> bytes:
    """Little-endian int16 samples, as `fmac.fir`/`fmac.iir`/`fmac.coef` take them."""
    return struct.pack(f"<{len(samples)}h", *samples)


def unpack(data: bytes) -> list[int]:
    return list(struct.unpack(f"<{len(data) // 2}h", data))


def crc(samples: Sequence[int]) -> str:
    return f"{zlib.crc32(pack(samples)):08x}"


def _history(history: Sequence[int], size: int) -> list[int]:
    """`size` samples, oldest first: the given ones (the newest `size` of them) after zeros."""
    kept = list(history)[-size:] if size else []
    return [0] * (size - len(kept)) + kept


def fir(
    b: Sequence[int],
    x: Sequence[int],
    gain: int = 0,
    clip: bool = False,
    history: Sequence[int] = (),
    arithmetic: Arithmetic = DEFAULT,
) -> tuple[list[int], bool]:
    """Outputs of a FIR of len(b) taps for the inputs `x`, and whether any output saturated or wrapped."""
    return iir(b, (), x, gain, clip, history, (), arithmetic)


def iir(
    b: Sequence[int],
    a: Sequence[int],
    x: Sequence[int],
    gain: int = 0,
    clip: bool = False,
    history: Sequence[int] = (),
    y_history: Sequence[int] = (),
    arithmetic: Arithmetic = DEFAULT,
) -> tuple[list[int], bool]:
    """Outputs of a direct-form-1 IIR (`a` = a1..aQ; empty for a FIR) and whether any output did not fit q1.15."""
    inputs = _history(history, len(b) - 1) + list(x)
    outputs = _history(y_history, len(a))
    results: list[int] = []
    saturated = False
    for n in range(len(b) - 1, len(inputs)):
        accumulator = 0
        for k, coefficient in enumerate(b):
            accumulator = arithmetic.wrap_accumulator(accumulator + arithmetic.product_of(coefficient, inputs[n - k]))
        for k, coefficient in enumerate(a, start=1):
            term = arithmetic.product_of(coefficient, outputs[-k])
            accumulator = arithmetic.wrap_accumulator(accumulator + arithmetic.feedback * term)
        value, overflow = arithmetic.output(accumulator, gain, clip)
        saturated |= overflow
        results.append(value)
        outputs.append(value)
    return results, saturated


@dataclass(frozen=True)
class Vector:
    """One calibration case: the stores (`b`, `a`, input history `xh`, output history `yh`, oldest first), the inputs
    and the options of `fmac.fir` (no `a`) or `fmac.iir`."""

    name: str
    b: tuple[int, ...]
    x: tuple[int, ...]
    a: tuple[int, ...] = ()
    gain: int = 0
    clip: bool = False
    xh: tuple[int, ...] = ()
    yh: tuple[int, ...] = ()

    @property
    def iir(self) -> bool:
        return bool(self.a)

    def evaluate(self, arithmetic: Arithmetic = DEFAULT) -> tuple[list[int], bool]:
        return iir(self.b, self.a, self.x, self.gain, self.clip, self.xh, self.yh, arithmetic)


# Each one tells two readings of the FMAC apart (tests/unit/test_fmac_ref.py has the alternatives' answers).
CALIBRATION: tuple[Vector, ...] = (
    # b[0] weights the newest sample: y = 0.5 * x[n - 1].
    Vector("order", b=(0, 0x4000), x=(0x7FFF, 0, 0x2000, 0)),
    # Truncation, not rounding: 0x7FFF * 1 is just below one output LSB.
    Vector("truncation", b=(0x7FFF, 0), x=(1, -1, 3, -3)),
    # Every product of 1 * 0x80 is below the accumulator LSB: 0 if each is truncated, 63 if summed exactly.
    Vector("products", b=(1,) * 127, x=(0x80,), gain=7, xh=(0x80,) * 126),
    # A sum of 6 fits q4.22 but not q3.23 (clipped to 32767 against a wrapped -32768).
    Vector("width", b=(0x7FFF,) * 6, x=(0x7FFF,), clip=True, xh=(0x7FFF,) * 5),
    # A sum of 12 wraps in q4.22 (clipped to -32768) where a saturating accumulator gives 32767.
    Vector("wrap", b=(0x7FFF,) * 12, x=(0x7FFF,), clip=True, xh=(0x7FFF,) * 11),
    # The gain shifts the accumulator before the output is taken.
    Vector("gain", b=(0x4000, 0), x=(0x0100, 0x0101, -0x0101, 0x7FFF), gain=3),
    # Outside q1.15: wrapped without clip.
    Vector("overflow", b=(0x7FFF, 0x7FFF), x=(0x7FFF, 0x7FFF, -0x8000, -0x8000)),
    # The input history is oldest first: y[0] = 0.5 * xh[-1] + 0.25 * xh[-2].
    Vector("input history", b=(0, 0x4000, 0x2000), x=(0, 0, 0), xh=(0x7FFF, 0x2000)),
    # Feedback is added: 0.25, 0.125, ... (subtracted it would alternate in sign).
    Vector("feedback", b=(0x4000, 0), a=(0x4000,), x=(0x4000, 0, 0, 0)),
    # The output history feeds the first outputs.
    Vector("output history", b=(0, 0), a=(0x4000, 0x2000), x=(0, 0, 0), yh=(0x4000, 0x7FFF)),
)


def generate_samples(count: int, seed: int, limit: int = Q15_MAX) -> list[int]:
    """`count` pseudo-random int16 samples in [-limit - 1, limit] (xorshift32 from `seed`), for coefficients and the
    host side of generated inputs."""
    state = seed or 1
    samples: list[int] = []
    for _ in range(count):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        value = state & 0xFFFF
        value = value - 0x10000 if value & 0x8000 else value
        samples.append(max(-limit - 1, min(limit, value * (limit + 1) // 0x8000)))
    return samples
