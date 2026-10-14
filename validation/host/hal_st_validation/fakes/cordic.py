"""Fake `cordic` group: the argument checks of validation/firmware/CordicGroup.cpp in their order, results from
`cordic_ref.simulate` (the reference plus an error that shrinks with the precision) and a duration per variant that
keeps `dma` faster than `it`, as on the board."""

from __future__ import annotations

from typing import cast

from .. import cordic_ref
from ..groups.cordic import ARGUMENT_BYTES_MAX, HEX_OUTPUT_MAX, VARIANTS
from ..patterns import crc_text
from .base import FakeGroup, _choice, _fail, _number, _payload, _shape

_KEYS = ("len", "pattern", "seed", "precision", "scale", "insize", "outsize", "nargs", "nres", "variant", "out")
_WIDTHS = ("32", "16")
# board::cordicDma of the STM32G474 board profile (write, read).
_DMA = (("dma2", 5), ("dma2", 6))
_OWNER = ("cordic", "dma")


def _duration_us(variant: str, calculations: int) -> int:
    if variant == "it":
        return 10 + 20 * calculations
    if variant == "dma":
        return 5 + calculations // 4
    return 2 + calculations // 2


class FakeCordic(FakeGroup):
    prefix = "cordic"

    def cmd_calc(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, _KEYS)
        function = args[0]
        if function not in cordic_ref.FUNCTIONS:
            _fail("usage")
        insize = int(_choice(options, "insize", _WIDTHS, "32"))
        outsize = int(_choice(options, "outsize", _WIDTHS, "32"))
        variant = _choice(options, "variant", VARIANTS, "poll")
        precision = _number(options.get("precision", "6"), 1, 15)
        scale = _number(options.get("scale", "0"), 0, 7)
        nargs = _number(options.get("nargs", "1"), 1, 2)
        nres = _number(options.get("nres", "1"), 1, 2)
        output = _choice(options, "out", ("hex", "crc"), "hex")
        payload = _payload(args, options, 1, ARGUMENT_BYTES_MAX)

        words = cordic_ref.argument_words(cast(cordic_ref.Width, insize), nargs)
        if not payload or len(payload) % (cordic_ref.WORD_BYTES * words):
            _fail("usage")
        calculations = len(payload) // cordic_ref.WORD_BYTES // words
        if (
            output == "hex"
            and calculations * cordic_ref.result_words(cast(cordic_ref.Width, outsize), nres) * cordic_ref.WORD_BYTES > HEX_OUTPUT_MAX
        ):
            _fail("range")
        if variant == "dma":
            self.fw.check_resources(_OWNER, _DMA)

        result = cordic_ref.simulate(
            cast(cordic_ref.Function, function),
            scale,
            precision,
            payload,
            cast(cordic_ref.Width, insize),
            cast(cordic_ref.Width, outsize),
            nargs,
            nres,
        )
        us = _duration_us(variant, calculations)
        if output == "crc":
            return f"OK len={len(result)} crc={crc_text(result)} us={us}"
        return f"OK data={result.hex()} us={us}"
