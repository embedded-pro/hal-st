"""QUADSPI group of validation/PROTOCOL.md (STM32WB55 only): `qspi` over `hal::QuadSpiStm` (`variant=poll`),
`hal::QuadSpiStmDma` (`variant=dma`) and `hal::SingleSpeedQuadSpiStmDma` as `hal::SpiMaster` (`variant=spi`), plus
the decoding of a logic-analyzer capture of the bus (pure functions).

Sources: validation/firmware/QuadSpiGroup.cpp (one instance, QUADSPI 1, pins of the board profile; every command
answers within 2000 ms or `ERR timeout`), `QuadSpiStm::CreateConfig` and `QuadSpiStmDma::SetConfig` (phases in the
order instruction, address, alternate bytes, dummy cycles, data; `lines` applies to every phase), EMIL
`hal::QuadSpi::AddressToVector` (address and alternate bytes go out most significant byte first), clock mode 0
(CLK idles low, the IO lines are sampled on the rising edge).
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

from .base import Group

__all__ = [
    "GROUPS",
    "QSPI_BUFFER",
    "QSPI_DUMMY_MAX",
    "QSPI_HEX_MAX",
    "QSPI_INDEX",
    "QSPI_PRESCALER_MAX",
    "QSPI_REPEAT_MAX",
    "QSPI_SIZE_MAX",
    "QSPI_TIMEOUT_S",
    "QuadSpi",
    "QuadSpiReply",
    "Variant",
    "qspi_bytes",
    "qspi_clock",
    "qspi_frame",
    "qspi_samples",
]

Variant = Literal["poll", "dma", "spi"]
Lines = Literal[1, 4]

QSPI_INDEX = 1
QSPI_BUFFER = 256
QSPI_HEX_MAX = 128
QSPI_PRESCALER_MAX = 255
QSPI_SIZE_MAX = 32
QSPI_DUMMY_MAX = 31
QSPI_REPEAT_MAX = 8
QSPI_TIMEOUT_S = 2.0


def qspi_clock(kernel: int, prescaler: int) -> int:
    """`clk=` of `qspi.open`: the QUADSPI kernel clock (HCLK4) divided by `prescaler + 1`."""
    return kernel // (prescaler + 1)


def qspi_frame(instr: int | None = None, address: bytes = b"", alternate: bytes = b"", data: bytes = b"") -> bytes:
    """The bytes of one command on the bus, without the dummy cycles: instruction, address, alternate bytes, data."""
    return (b"" if instr is None else bytes([instr])) + address + alternate + data


def qspi_samples(clk: Sequence[int], ncs: Sequence[int], ios: Sequence[Sequence[int]]) -> list[list[int]]:
    """One list per chip-select-low region: the IO levels at each rising CLK edge, as nibbles with IO0 in bit 0."""
    frames: list[list[int]] = []
    current: list[int] | None = None
    for index in range(1, len(clk)):
        if ncs[index]:
            if current:
                frames.append(current)
            current = None
            continue
        if current is None:
            current = []
        if clk[index] and not clk[index - 1]:
            current.append(sum((io[index] & 1) << bit for bit, io in enumerate(ios)))
    if current:
        frames.append(current)
    return frames


def qspi_bytes(samples: Sequence[int], lines: int, line: int = 0) -> bytes:
    """Bytes of consecutive samples, most significant bit first: 4 lines carry a nibble per clock (IO3 is bit 3),
    1 line one bit per clock on IO `line` (IO0 out, IO1 in). A partial last byte is dropped."""
    if lines == 4:
        return bytes((samples[i] << 4 | samples[i + 1]) & 0xFF for i in range(0, len(samples) - 1, 2))
    if lines != 1:
        raise ValueError(f"lines must be 1 or 4: {lines}")
    result = bytearray()
    for start in range(0, len(samples) - 7, 8):
        value = 0
        for sample in samples[start : start + 8]:
            value = value << 1 | (sample >> line) & 1
        result.append(value)
    return bytes(result)


@dataclass(frozen=True)
class QuadSpiReply:
    """A write answers `flevel` (the FIFO level sampled in the completion callback, B.15), a read its `data`, or
    with `out=crc` its length and CRC-32 (`crc` as 8 hex digits)."""

    data: bytes | None = None
    flevel: int | None = None
    crc: str | None = None


class QuadSpi(Group):
    prefix = "qspi"

    def _timeout(self, cmd_timeout: float | None) -> float:
        return cmd_timeout or self._fw.terminal.timeout + QSPI_TIMEOUT_S

    def open(
        self,
        index: int,
        variant: Variant | None = None,
        prescaler: int | None = None,
        size: int | None = None,
    ) -> int:
        """Returns `clk`, the QUADSPI clock; `size` is the flash size as log2 of bytes (DCR FSIZE + 1)."""
        response = self._cmd("open", index, variant=variant, prescaler=prescaler, size=size)
        self._fw.track((self.prefix, index), f"{self.prefix}.close", index)
        return response.as_int("clk")

    def cmd(
        self,
        index: int,
        instr: int | None = None,
        addr: int | None = None,
        abytes: int | None = None,
        alt: int | None = None,
        altbytes: int | None = None,
        dummy: int | None = None,
        lines: Lines | None = None,
        tx: bytes | None = None,
        len: int | None = None,
        pattern: str | None = None,
        seed: int | None = None,
        rx: int | None = None,
        out: Literal["hex", "crc"] | None = None,
        repeat: int | None = None,
        cmd_timeout: float | None = None,
    ) -> QuadSpiReply:
        """`hal::QuadSpi::SendData` (`tx`, or `len`/`pattern`/`seed`, or no data phase) or `ReceiveData` (`rx`);
        `repeat` issues the write again from each completion callback. Every phase is optional."""
        response = self._cmd(
            "cmd",
            index,
            instr=instr,
            addr=addr,
            abytes=abytes,
            alt=alt,
            altbytes=altbytes,
            dummy=dummy,
            lines=lines,
            tx=tx,
            len=len,
            pattern=pattern,
            seed=seed,
            rx=rx,
            out=out,
            repeat=repeat,
            cmd_timeout=self._timeout(cmd_timeout),
        )
        if rx is None:
            return QuadSpiReply(flevel=response.as_int("flevel"))
        if "crc" in response:
            return QuadSpiReply(crc=response["crc"])
        return QuadSpiReply(data=response.as_bytes("data"))

    def poll(
        self,
        index: int,
        match: int,
        mask: int,
        size: int | None = None,
        instr: int | None = None,
        addr: int | None = None,
        abytes: int | None = None,
        alt: int | None = None,
        altbytes: int | None = None,
        dummy: int | None = None,
        lines: Lines | None = None,
        cmd_timeout: float | None = None,
    ) -> None:
        """`hal::QuadSpi::PollStatus`: answers once the status bytes, masked with `mask`, equal `match`."""
        self._cmd(
            "poll",
            index,
            instr=instr,
            addr=addr,
            abytes=abytes,
            alt=alt,
            altbytes=altbytes,
            dummy=dummy,
            lines=lines,
            match=match,
            mask=mask,
            size=size,
            cmd_timeout=self._timeout(cmd_timeout),
        )

    def xfer(
        self,
        index: int,
        tx: bytes = b"",
        rx: int | None = None,
        len: int | None = None,
        pattern: str | None = None,
        seed: int | None = None,
        repeat: int | None = None,
        cmd_timeout: float | None = None,
    ) -> QuadSpiReply:
        """`hal::SingleSpeedQuadSpiStmDma::SendAndReceive` (`variant=spi`), half duplex: `tx` (or `len`) or `rx`."""
        response = self._cmd(
            "xfer",
            index,
            tx,
            len=len,
            pattern=pattern,
            seed=seed,
            rx=rx,
            repeat=repeat,
            cmd_timeout=self._timeout(cmd_timeout),
        )
        if rx is None:
            return QuadSpiReply(flevel=response.as_int("flevel"))
        return QuadSpiReply(data=response.as_bytes("rx"))

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack((self.prefix, index))


GROUPS: dict[str, type[Group]] = {"qspi": QuadSpi}
