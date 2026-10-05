"""SPI frames of 4 to 16 bits: a logic-capture decoder and the frame model of `hal::SpiMasterStmDma` with a
`hal::SpiDataSizeConfiguratorStm` (`spi.open ... dma=1 bits=<n>`).

`spi_decode_words` decodes like `ad3_waveforms_bench.analysis.spi_decode` but keeps whole words: the bench decoder
masks every word to 8 bits.

The frame model (`spi_frames`, `spi_bytes`) follows the driver, not the API's byte view: with up to 8 bits every
buffer byte is one frame (its low `bits` bits); above 8 bits `SpiMasterStmDma::SendAndReceive` hands the DMA the
buffer as 16-bit words (SpiMasterStmDma.cpp:109-129, memory width `sizeof(uint16_t)`, DmaStm.hpp:461), so every
frame takes two buffer bytes, little endian, and a buffer needs an even length (the count is bytes / 2,
DmaStm.cpp:816; GPDMA takes the byte count, DmaStm.cpp:812). Received frames come back right aligned, the bits above
the frame zero.
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass, field

from ad3_waveforms_bench.analysis import Bits, runs, spi_mode_bits

__all__ = ["BITS_MAX", "BITS_MIN", "SpiWords", "spi_bytes", "spi_decode_words", "spi_frames", "spi_join_words"]

BITS_MIN = 4
BITS_MAX = 16
_BYTE_BITS = 8


@dataclass
class SpiWords:
    """One chip-select-active region (or the whole capture): its words, as wide as `bits`."""

    mosi: list[int] = field(default_factory=list)
    miso: list[int] = field(default_factory=list)
    clock_edges: int = 0


def _check_bits(bits: int) -> None:
    if not BITS_MIN <= bits <= BITS_MAX:
        raise ValueError(f"frame size {bits} outside {BITS_MIN}..{BITS_MAX} bits")


def spi_decode_words(
    clk: Bits,
    mosi: Bits,
    miso: Bits | None = None,
    cs: Bits | None = None,
    mode: int = 0,
    bits: int = 8,
    msb_first: bool = True,
    cs_active_low: bool = True,
) -> list[SpiWords]:
    """Decode `bits`-wide words from sampled lines. Each chip-select-active region is one entry, without `cs` the
    whole capture is one; a partial word at the end of a region is dropped."""
    if bits < 1:
        raise ValueError(f"word size {bits}")
    cpol, cpha = spi_mode_bits(mode)
    sample_on_rising = cpol == cpha
    active = 0 if cs_active_low else 1
    regions = [(0, len(clk))] if cs is None else [(start, start + length) for level, start, length in runs(cs) if level == active]
    result: list[SpiWords] = []
    for start, end in regions:
        words = SpiWords()
        word_mosi = word_miso = 0
        count = 0
        for i in range(max(start, 1), end):
            if clk[i] == clk[i - 1] or bool(clk[i]) != sample_on_rising:
                continue
            words.clock_edges += 1
            bit_mosi = int(mosi[i])
            bit_miso = int(miso[i]) if miso is not None else 0
            if msb_first:
                word_mosi = (word_mosi << 1) | bit_mosi
                word_miso = (word_miso << 1) | bit_miso
            else:
                word_mosi |= bit_mosi << count
                word_miso |= bit_miso << count
            count += 1
            if count == bits:
                words.mosi.append(word_mosi)
                words.miso.append(word_miso)
                word_mosi = word_miso = 0
                count = 0
        if words.clock_edges:
            result.append(words)
    return result


def spi_join_words(regions: Sequence[SpiWords]) -> tuple[list[int], list[int]]:
    """The MOSI and MISO words of every region, in order."""
    return [word for region in regions for word in region.mosi], [word for region in regions for word in region.miso]


def spi_frames(data: bytes, bits: int) -> list[int]:
    """The frames `SpiMasterStmDma` clocks for the buffer `data` with frames of `bits` bits."""
    _check_bits(bits)
    mask = (1 << bits) - 1
    if bits <= _BYTE_BITS:
        return [byte & mask for byte in data]
    if len(data) % 2:
        raise ValueError(f"frames of {bits} bits take two buffer bytes each: {len(data)} bytes")
    return [int.from_bytes(data[i : i + 2], "little") & mask for i in range(0, len(data), 2)]


def spi_bytes(frames: Sequence[int], bits: int) -> bytes:
    """The receive buffer `SpiMasterStmDma` fills with `frames` of `bits` bits (the inverse of `spi_frames` for
    the bits a frame carries)."""
    _check_bits(bits)
    mask = (1 << bits) - 1
    if bits <= _BYTE_BITS:
        return bytes(frame & mask for frame in frames)
    return b"".join((frame & mask).to_bytes(2, "little") for frame in frames)
