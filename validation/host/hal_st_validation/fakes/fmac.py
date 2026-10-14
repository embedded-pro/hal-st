"""Fake `fmac` group: the stores and argument checks of validation/firmware/FmacGroup.cpp in their order, outputs
from `fmac_ref` and a duration per variant that keeps `dma` faster than `it`, as on the board."""

from __future__ import annotations

from typing import TYPE_CHECKING

from .. import fmac_ref
from ..groups.fmac import HEX_OUTPUT_MAX, INPUT_BYTES_MAX, STORES, VARIANTS
from ..patterns import crc_text
from .base import FakeGroup, _choice, _fail, _flag, _hex, _number, _payload, _shape

if TYPE_CHECKING:
    from ..fake_firmware import FakeFirmware

_FIR_KEYS = ("taps", "gain", "clip", "hist", "variant", "out", "len", "pattern", "seed")
_IIR_KEYS = ("p", "q", "gain", "clip", "hist", "yhist", "variant", "out", "len", "pattern", "seed")
# board::fmacDma of the STM32G474 board profile (write, read).
_DMA = (("dma2", 7), ("dma2", 8))
_OWNER = ("fmac", "dma")


def _duration_us(variant: str, samples: int) -> int:
    if variant == "it":
        return 10 + 15 * samples
    if variant == "dma":
        return 5 + samples // 4
    return 2 + samples // 2


class FakeFmac(FakeGroup):
    prefix = "fmac"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        self.boot()

    def boot(self) -> None:
        self.stores = {name: [0] * size for name, size in STORES.items()}

    def cmd_load(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 3, 3, ("clear",))
        store = args[0]
        if store not in STORES:
            _fail("usage")
        offset = _number(args[1], 0, STORES["b"] - 1)
        clear = _flag(options, "clear")
        data = _hex(args[2], 2 * STORES["b"])
        if not data or len(data) % 2:
            _fail("usage")
        samples = fmac_ref.unpack(data)
        target = self.stores[store]
        if offset + len(samples) > len(target):
            _fail("range")
        if clear:
            target[:] = [0] * len(target)
        target[offset : offset + len(samples)] = samples
        return f"OK size={offset + len(samples)}"

    def cmd_fir(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _FIR_KEYS)
        if "taps" not in options:
            _fail("usage")
        p = _number(options["taps"], 2, 127)
        return self._run(args, options, p, 0, 0)

    def cmd_iir(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _IIR_KEYS)
        if "p" not in options or "q" not in options:
            _fail("usage")
        p = _number(options["p"], 2, 64)
        q = _number(options["q"], 1, 63)
        yhist = _number(options.get("yhist", "0"), 0, STORES["y"])
        return self._run(args, options, p, q, yhist)

    def _run(self, args: list[str], options: dict[str, str], p: int, q: int, yhist: int) -> str:
        gain = _number(options.get("gain", "0"), 0, 7)
        clip = _flag(options, "clip")
        hist = _number(options.get("hist", "0"), 0, STORES["x"])
        variant = _choice(options, "variant", VARIANTS, "poll")
        output = _choice(options, "out", ("hex", "crc"), "hex")
        payload = _payload(args, options, 0, INPUT_BYTES_MAX)
        if not payload or len(payload) % 2:
            _fail("usage")
        if output == "hex" and len(payload) > HEX_OUTPUT_MAX:
            _fail("range")
        if variant == "dma":
            self.fw.check_resources(_OWNER, _DMA)

        x = fmac_ref.unpack(payload)
        b, a = self.stores["b"][:p], self.stores["a"][:q]
        result, saturated = fmac_ref.iir(b, a, x, gain, clip, self.stores["x"][:hist], self.stores["y"][:yhist])
        tail = f"sat={int(saturated)} us={_duration_us(variant, len(x))}"
        if output == "crc":
            return f"OK len={len(payload)} crc={crc_text(fmac_ref.pack(result))} {tail}"
        return f"OK data={fmac_ref.pack(result).hex()} {tail}"
