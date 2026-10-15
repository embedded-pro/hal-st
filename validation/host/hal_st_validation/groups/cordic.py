"""`cordic` group of validation/PROTOCOL.md: `cordic.calc` runs one batch of calculations on the STM32G474 CORDIC
(`hal::SynchronousCordicStm` for `variant=poll|zo`, `hal::CordicStm` for `it`, `hal::CordicStmDma` for `dma`) and
answers the result words and the time the batch took. Stateless: nothing to close. The reference model is
`cordic_ref`."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from .base import Group

__all__ = ["GROUPS", "VARIANTS", "Cordic", "CordicResult"]

Variant = Literal["poll", "zo", "it", "dma"]
VARIANTS: tuple[Variant, ...] = ("poll", "zo", "it", "dma")
# Bytes of arguments one `cordic.calc` takes (`len=` or hex) and of results it answers as hex.
ARGUMENT_BYTES_MAX = 2048
HEX_OUTPUT_MAX = 128


@dataclass(frozen=True)
class CordicResult:
    """`data` with `out=hex`; `length` and `crc` (8 hex digits) with `out=crc`."""

    us: int
    data: bytes | None = None
    length: int | None = None
    crc: str | None = None


class Cordic(Group):
    prefix = "cordic"

    def calc(
        self,
        function: str,
        data: bytes | None = None,
        *,
        len: int | None = None,
        pattern: str | None = None,
        seed: int | None = None,
        precision: int | None = None,
        scale: int | None = None,
        insize: int | None = None,
        outsize: int | None = None,
        nargs: int | None = None,
        nres: int | None = None,
        variant: Variant | None = None,
        out: Literal["hex", "crc"] | None = None,
    ) -> CordicResult:
        """`data` is the argument words (little-endian, `cordic_ref.pack_arguments`), or None for `-` with `len`."""
        response = self._cmd(
            "calc",
            function,
            "-" if data is None else data,
            len=len,
            pattern=pattern,
            seed=seed,
            precision=precision,
            scale=scale,
            insize=insize,
            outsize=outsize,
            nargs=nargs,
            nres=nres,
            variant=variant,
            out=out,
        )
        if "crc" in response:
            return CordicResult(us=response.as_int("us"), length=response.as_int("len"), crc=response["crc"])
        return CordicResult(us=response.as_int("us"), data=response.as_bytes("data"))


GROUPS: dict[str, type[Group]] = {"cordic": Cordic}
