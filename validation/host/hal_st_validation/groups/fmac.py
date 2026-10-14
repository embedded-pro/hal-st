"""`fmac` group of validation/PROTOCOL.md: `fmac.load` fills the coefficient and history stores of the firmware,
`fmac.fir` and `fmac.iir` filter one block of samples on the STM32G474 FMAC (`hal::SynchronousFmacStm` for
`variant=poll`, `hal::FmacStm` for `it`, `hal::FmacStmDma` for `dma`) and answer the output samples, the saturation
flag and the duration. The stores keep their contents until the next reset; nothing to close. The reference model is
`fmac_ref`."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

from .. import fmac_ref
from .base import Group

__all__ = ["GROUPS", "LOAD_CHUNK", "STORES", "VARIANTS", "Fmac", "FmacResult"]

Variant = Literal["poll", "it", "dma"]
Store = Literal["b", "a", "x", "y"]
VARIANTS: tuple[Variant, ...] = ("poll", "it", "dma")
# Store sizes in samples: b 127 taps, a 63 feedback coefficients, x 126 input and y 63 output history samples.
STORES: dict[str, int] = {"b": 127, "a": 63, "x": 126, "y": 63}
# Samples per `fmac.load` that keep the line within 255 characters.
LOAD_CHUNK = 50
INPUT_BYTES_MAX = 2048
HEX_OUTPUT_MAX = 128


@dataclass(frozen=True)
class FmacResult:
    """`samples` with `out=hex`; `length` (bytes) and `crc` with `out=crc`."""

    sat: bool
    us: int
    samples: list[int] | None = None
    length: int | None = None
    crc: str | None = None


class Fmac(Group):
    prefix = "fmac"

    def load(self, store: Store, offset: int, samples: Sequence[int], clear: bool | None = None) -> int:
        return self._cmd("load", store, offset, fmac_ref.pack(samples), clear=clear).as_int("size")

    def load_all(self, store: Store, samples: Sequence[int]) -> None:
        """The whole store: `samples` from 0, the rest zero (one `fmac.load` per `LOAD_CHUNK` samples)."""
        padded = list(samples) + [0] * (STORES[store] - len(samples))
        for offset in range(0, len(padded), LOAD_CHUNK):
            self.load(store, offset, padded[offset : offset + LOAD_CHUNK])

    def _result(self, response) -> FmacResult:
        sat, us = response.as_bool("sat"), response.as_int("us")
        if "crc" in response:
            return FmacResult(sat=sat, us=us, length=response.as_int("len"), crc=response["crc"])
        return FmacResult(sat=sat, us=us, samples=fmac_ref.unpack(response.as_bytes("data")))

    def fir(
        self,
        samples: Sequence[int] | None,
        taps: int,
        *,
        gain: int | None = None,
        clip: bool | None = None,
        hist: int | None = None,
        len: int | None = None,
        pattern: str | None = None,
        seed: int | None = None,
        variant: Variant | None = None,
        out: Literal["hex", "crc"] | None = None,
    ) -> FmacResult:
        """`samples` are int16 inputs, or None for `-` with `len` (bytes)."""
        data = "-" if samples is None else fmac_ref.pack(samples)
        response = self._cmd(
            "fir", data, taps=taps, gain=gain, clip=clip, hist=hist, len=len, pattern=pattern, seed=seed, variant=variant, out=out
        )
        return self._result(response)

    def iir(
        self,
        samples: Sequence[int] | None,
        p: int,
        q: int,
        *,
        gain: int | None = None,
        clip: bool | None = None,
        hist: int | None = None,
        yhist: int | None = None,
        len: int | None = None,
        pattern: str | None = None,
        seed: int | None = None,
        variant: Variant | None = None,
        out: Literal["hex", "crc"] | None = None,
    ) -> FmacResult:
        data = "-" if samples is None else fmac_ref.pack(samples)
        response = self._cmd(
            "iir",
            data,
            p=p,
            q=q,
            yhist=yhist,
            gain=gain,
            clip=clip,
            hist=hist,
            len=len,
            pattern=pattern,
            seed=seed,
            variant=variant,
            out=out,
        )
        return self._result(response)


GROUPS: dict[str, type[Group]] = {"fmac": Fmac}
