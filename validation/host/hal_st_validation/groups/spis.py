"""SPI slave group of validation/PROTOCOL.md: `spis` (`hal::SpiSlaveStmDma`, validation/firmware/SpiSlaveGroup.cpp).

`spis.arm` hands the driver one transfer and answers at once; `spis.result` answers when that transfer is done, or
`done=0` after `wait` ms (default 1000) with the transfer still armed. A second `spis.result` while one waits
answers `ERR busy`; `spis.cancel` and `spis.close` answer a waiting `spis.result` with `done=0` first.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from ad3_waveforms_bench.protocol import Response
from ad3_waveforms_bench.terminal import PendingCommand

from ..patterns import Pattern
from .base import Group, Pin

__all__ = ["GROUPS", "SPIS_BUFFER", "SPIS_HEX_MAX", "SPIS_WAIT_DEFAULT_MS", "SPIS_WAIT_MAX_MS", "SpiSlave", "SpisResult"]

SPIS_BUFFER = 1024
SPIS_HEX_MAX = 128
SPIS_WAIT_DEFAULT_MS = 1000
SPIS_WAIT_MAX_MS = 10000

Output = Literal["hex", "crc"]


@dataclass(frozen=True)
class SpisResult:
    """`done` with the received bytes (`rx`, hex output) or their `length` and `crc` (`out=crc`); not done, none."""

    done: bool
    rx: bytes | None = None
    length: int | None = None
    crc: str | None = None

    @classmethod
    def parse(cls, response: Response) -> SpisResult:
        if not response.as_bool("done"):
            return cls(False)
        if "crc" in response:
            return cls(True, length=response.as_int("len"), crc=response["crc"])
        return cls(True, rx=response.as_bytes("rx"))


class SpiSlave(Group):
    prefix = "spis"

    def _timeout(self, wait: int | None, cmd_timeout: float | None) -> float:
        return cmd_timeout or self._fw.terminal.timeout + (SPIS_WAIT_DEFAULT_MS if wait is None else wait) / 1000

    def open(self, index: int, clk: Pin, miso: Pin, mosi: Pin, nss: Pin) -> None:
        self._cmd("open", index, clk=self._pin(clk), miso=self._pin(miso), mosi=self._pin(mosi), nss=self._pin(nss))
        self._fw.track((self.prefix, index), f"{self.prefix}.close", index)

    def arm(
        self,
        index: int,
        tx: bytes = b"",
        rx: int | None = None,
        len: int | None = None,
        pattern: Pattern | None = None,
        seed: int | None = None,
    ) -> None:
        """Full duplex: `tx` (or `len=`) with `rx` left out or equal; send only: `rx=0`; receive only: no `tx`, `rx=n`."""
        self._cmd("arm", index, bytes(tx), rx=rx, len=len, pattern=pattern, seed=seed)

    def result(self, index: int, wait: int | None = None, out: Output | None = None, cmd_timeout: float | None = None) -> SpisResult:
        return SpisResult.parse(self._cmd("result", index, wait=wait, out=out, cmd_timeout=self._timeout(wait, cmd_timeout)))

    def begin_result(self, index: int, wait: int | None = None, out: Output | None = None) -> PendingCommand:
        """`spis.result` without waiting for its final line (`SpisResult.parse(settle(pending))`)."""
        return self.begin("result", index, wait=wait, out=out, cmd_timeout=self._timeout(wait, None))

    def cancel(self, index: int) -> bool:
        """True when a transfer was still running."""
        return self._cmd("cancel", index).as_bool("cancelled")

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack((self.prefix, index))


GROUPS = {"spis": SpiSlave}
