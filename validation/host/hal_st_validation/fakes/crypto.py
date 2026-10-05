"""Fake `rng`, `aes` and `pka` groups: the argument checks of validation/firmware/RngGroup.cpp, AesGroup.cpp and
PkaGroup.cpp in their order, a deterministic RNG (the `prbs` payload pattern from a new seed per call, so two reads
differ), and AES/PKA results from `crypto_ref`.

`variant=hsem` exists on STM32WB55 only; its `hsi48=` is the fake clock group's HSI48 state, which the driver leaves
as it found it (it switches HSI48 on for the read only when it was off). `lock5=1` with HSI48 off answers
`ERR failed`, as the firmware refuses what would trip the driver's assertion."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from .. import crypto_ref, patterns, rngstats
from ..groups.crypto import VARIANTS
from .base import FakeGroup, _choice, _fail, _flag, _number, _shape

if TYPE_CHECKING:
    from ..fake_firmware import FakeFirmware

_READ_MAX = 128
_STATS_MIN = 16
_STATS_MAX = 65536
_AES_DATA_MAX = 80
_AES_BLOCK = 16
_OPERAND = 32
_COMPARE_MIN = 4
_COMPARE_MAX = 60
# A P-256 scalar multiplication on the PKA takes some ten milliseconds; the fake reports a fixed duration.
_MULTIPLY_US = 20000
_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")


class FakeRng(FakeGroup):
    prefix = "rng"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        self.boot()

    def boot(self) -> None:
        self.seed = 0

    def _generate(self, length: int) -> bytes:
        self.seed = (self.seed + 0x9E3779B9) & patterns.SEED_MAX
        return patterns.generate(length, "prbs", self.seed)

    def _check(self, variant: str, lock5: bool) -> None:
        if (variant == "hsem" or lock5) and self.fw.family != "stm32wb55":
            _fail("unsupported")
        if lock5 and variant != "hsem":
            _fail("usage")

    def _hsi48(self) -> int:
        clock = self.fw.group("clock")
        return int(getattr(clock, "hsi48", 1))

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("variant", "lock5"))
        length = _number(args[0], 1, _READ_MAX)
        variant = _choice(options, "variant", VARIANTS, "sync")
        lock5 = _flag(options, "lock5")
        self._check(variant, lock5)
        if lock5 and not self._hsi48():
            _fail("failed")
        line = f"OK data={self._generate(length).hex()}"
        if variant == "hsem":
            line += f" hsi48={self._hsi48()}"
        return line

    def cmd_stats(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("variant",))
        length = _number(args[0], _STATS_MIN, _STATS_MAX)
        variant = _choice(options, "variant", VARIANTS, "sync")
        self._check(variant, False)
        stats = rngstats.byte_stats(self._generate(length))
        return f"OK n={stats.n} ones={stats.ones} runs={stats.runs} chisq={stats.chisq_x1000} crc={stats.crc:08x} us={length // 4 + 50}"


def _blocks(text: str, capacity: int) -> bytes:
    """`ParseBlocks` of AesGroup.cpp: every malformed, empty, too long or partial-block hex is `ERR usage`."""
    if text == "-" or not text or len(text) % 2 or len(text) // 2 > capacity or not set(text) <= _HEX_DIGITS:
        _fail("usage")
    data = bytes.fromhex(text)
    if len(data) % _AES_BLOCK:
        _fail("usage")
    return data


class FakeAes(FakeGroup):
    prefix = "aes"

    def _run(self, args: list[str], options: dict[str, str], decrypt: bool) -> str:
        _shape(args, options, 2, 2, ("swap",))
        key = _blocks(args[0], _AES_BLOCK)
        data = _blocks(args[1], _AES_DATA_MAX)
        swap = _choice(options, "swap", crypto_ref.SWAPS, "byte")
        return f"OK data={crypto_ref.aes_stm(key, data, cast(crypto_ref.Swap, swap), decrypt).hex()}"

    def cmd_enc(self, args: list[str], options: dict[str, str]) -> str:
        return self._run(args, options, False)

    def cmd_dec(self, args: list[str], options: dict[str, str]) -> str:
        return self._run(args, options, True)


def _hex_size(text: str | None) -> int | None:
    """`HexSize` of PkaGroup.cpp: the byte count of a non-empty, even, all-hex operand, else None (`ERR usage`)."""
    if not text or len(text) % 2 or not set(text) <= _HEX_DIGITS:
        return None
    return len(text) // 2


def _operand(text: str) -> int:
    return int(text, 16)


class FakePka(FakeGroup):
    prefix = "pka"

    def cmd_mul(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("k", "x", "y"))
        k, x, y = options.get("k"), options.get("x"), options.get("y")
        sizes = [_hex_size(text) for text in (k, x, y) if text is not None]
        if (x is None) != (y is None) or None in sizes:
            _fail("usage")
        if any(size is not None and size > _OPERAND for size in sizes):
            _fail("range")
        curve = crypto_ref.P256
        point = None if x is None or y is None else (_operand(x), _operand(y))
        result = crypto_ref.scalar_multiply(curve, 1 if k is None else _operand(k), point)
        rx, ry = result if result is not None else (0, 0)
        return f"OK x={crypto_ref.to_bytes(rx).hex()} y={crypto_ref.to_bytes(ry).hex()} us={_MULTIPLY_US}"

    def cmd_check(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("x", "y"))
        x, y = options.get("x"), options.get("y")
        sizes = [_hex_size(x), _hex_size(y)]
        if None in sizes:
            _fail("usage")
        if any(size is not None and size > _OPERAND for size in sizes):
            _fail("range")
        assert x is not None and y is not None
        return f"OK on={int(crypto_ref.P256.on_curve((_operand(x), _operand(y))))}"

    def cmd_cmp(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("a", "b"))
        a, b = options.get("a"), options.get("b")
        size, size_b = _hex_size(a), _hex_size(b)
        if size is None or size != size_b:
            _fail("usage")
        assert size is not None and a is not None and b is not None
        if not _COMPARE_MIN <= size <= _COMPARE_MAX:
            _fail("range")
        first, second = _operand(a), _operand(b)
        return "OK cmp=" + ("eq" if first == second else "gt" if first > second else "lt")
