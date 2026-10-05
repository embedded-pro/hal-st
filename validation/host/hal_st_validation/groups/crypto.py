"""Crypto groups of validation/PROTOCOL.md: `rng` (`hal::SynchronousRandomDataGeneratorStm`, `variant=async`
`hal::RandomDataGeneratorStm`, `variant=hsem` `hal::SynchronousSynchronizedRandomDataGeneratorStm` on STM32WB55),
`aes` (`hal::SynchronousAes128EcbStm`) and `pka` (`hal::PkaStm` on `services::secp256r1`). All three are stateless:
nothing to close. The reference models are `crypto_ref` and `rngstats`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from .base import Group

__all__ = ["GROUPS", "VARIANTS", "Aes", "Pka", "PkaProduct", "Rng", "RngRead", "RngStats"]

Variant = Literal["sync", "async", "hsem"]
Swap = Literal["none", "half", "byte", "bit"]

VARIANTS: tuple[Variant, ...] = ("sync", "async", "hsem")


@dataclass(frozen=True)
class RngRead:
    data: bytes
    # `LL_RCC_HSI48_IsReady` after a `variant=hsem` read, else None.
    hsi48: int | None
    raw: str


@dataclass(frozen=True)
class RngStats:
    n: int
    ones: int
    runs: int
    chisq_x1000: int
    crc: int
    us: int
    raw: str


class Rng(Group):
    prefix = "rng"

    def read(self, length: int, variant: Variant | None = None, lock5: bool | None = None) -> RngRead:
        response = self._cmd("read", length, variant=variant, lock5=lock5)
        hsi48 = response.get("hsi48")
        return RngRead(data=response.as_bytes("data"), hsi48=None if hsi48 is None else int(hsi48), raw=response.raw)

    def stats(self, length: int, variant: Variant | None = None, cmd_timeout: float | None = None) -> RngStats:
        response = self._cmd("stats", length, variant=variant, cmd_timeout=cmd_timeout)
        return RngStats(
            n=response.as_int("n"),
            ones=response.as_int("ones"),
            runs=response.as_int("runs"),
            chisq_x1000=response.as_int("chisq"),
            crc=int(response["crc"], 16),
            us=response.as_int("us"),
            raw=response.raw,
        )


class Aes(Group):
    prefix = "aes"

    def enc(self, key: bytes, data: bytes, swap: Swap | None = None) -> bytes:
        return self._cmd("enc", key, data, swap=swap).as_bytes("data")

    def dec(self, key: bytes, data: bytes, swap: Swap | None = None) -> bytes:
        return self._cmd("dec", key, data, swap=swap).as_bytes("data")


@dataclass(frozen=True)
class PkaProduct:
    x: bytes
    y: bytes
    us: int


class Pka(Group):
    """Operands are big-endian byte strings, 1-32 bytes (the firmware pads them to 32), or `cmp` 4-60 bytes each."""

    prefix = "pka"

    def mul(self, k: bytes | None = None, x: bytes | None = None, y: bytes | None = None, cmd_timeout: float | None = None) -> PkaProduct:
        response = self._cmd("mul", k=k, x=x, y=y, cmd_timeout=cmd_timeout)
        return PkaProduct(x=response.as_bytes("x"), y=response.as_bytes("y"), us=response.as_int("us"))

    def check(self, x: bytes, y: bytes) -> bool:
        return self._cmd("check", x=x, y=y).as_bool("on")

    def cmp(self, a: bytes, b: bytes) -> str:
        return self._cmd("cmp", a=a, b=b)["cmp"]


GROUPS: dict[str, type[Group]] = {"rng": Rng, "aes": Aes, "pka": Pka}
