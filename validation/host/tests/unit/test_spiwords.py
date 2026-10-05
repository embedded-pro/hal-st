"""`spiwords`: decoding words of 4..16 bits from sampled lines, and the frame model of `SpiMasterStmDma` with a
data size configurator (one frame per byte up to 8 bits, one per little-endian byte pair above)."""

import pytest
from ad3_waveforms_bench import analysis

from hal_st_validation.spiwords import BITS_MAX, BITS_MIN, spi_bytes, spi_decode_words, spi_frames, spi_join_words


def waveform(regions, bits, mode=0, msb_first=True, gap=3):
    """Sampled clk, mosi, miso and cs for `regions` (lists of (mosi word, miso word)): four samples per bit, data
    changing on the shift edge, chip select low around each region."""
    cpol, cpha = analysis.spi_mode_bits(mode)
    idle, active = cpol, 1 - cpol
    clk, mosi, miso, cs = [], [], [], []

    def emit(count, clock, out, back, select):
        clk.extend([clock] * count)
        mosi.extend([out] * count)
        miso.extend([back] * count)
        cs.extend([select] * count)

    emit(gap, idle, 0, 0, 1)
    for words in regions:
        emit(gap, idle, 0, 0, 0)
        for out_word, back_word in words:
            order = range(bits - 1, -1, -1) if msb_first else range(bits)
            for bit in order:
                out, back = (out_word >> bit) & 1, (back_word >> bit) & 1
                first, second = (idle, active) if cpha == 0 else (active, idle)
                emit(2, first, out, back, 0)
                emit(2, second, out, back, 0)
        emit(gap, idle, 0, 0, 0)
        emit(gap, idle, 0, 0, 1)
    return clk, mosi, miso, cs


@pytest.mark.parametrize("mode", [0, 1, 2, 3])
@pytest.mark.parametrize("bits", [4, 7, 8, 9, 12, 16])
@pytest.mark.parametrize("msb_first", [True, False])
def test_decode_round_trip(mode, bits, msb_first):
    mask = (1 << bits) - 1
    words = [(0xA5A5 & mask, 0x5A5A & mask), (mask, 0), (1, 1 << (bits - 1)), (0x1234 & mask, 0xFEDC & mask)]
    clk, mosi, miso, cs = waveform([words], bits, mode, msb_first)
    regions = spi_decode_words(clk, mosi, miso, cs, mode=mode, bits=bits, msb_first=msb_first)
    assert len(regions) == 1
    assert regions[0].mosi == [out for out, _ in words]
    assert regions[0].miso == [back for _, back in words]
    assert regions[0].clock_edges == bits * len(words)


def test_words_are_not_masked_to_bytes():
    """The bench decoder keeps 8 bits of every word; `spi_decode_words` keeps all of them."""
    clk, mosi, miso, cs = waveform([[(0xABC, 0x123)]], 12)
    assert spi_decode_words(clk, mosi, miso, cs, bits=12)[0].mosi == [0xABC]
    assert analysis.spi_decode(clk, mosi, miso, cs, bits_per_word=12)[0].mosi == bytearray([0xBC])


def test_regions_follow_the_chip_select_and_join():
    clk, mosi, miso, cs = waveform([[(1, 2), (3, 4)], [(5, 6)]], 16)
    regions = spi_decode_words(clk, mosi, miso, cs, bits=16)
    assert [region.mosi for region in regions] == [[1, 3], [5]]
    assert spi_join_words(regions) == ([1, 3, 5], [2, 4, 6])


def test_without_chip_select_the_capture_is_one_region_and_a_partial_word_is_dropped():
    clk, mosi, miso, _ = waveform([[(0x9, 0x6), (0xF, 0x0)]], 4)
    regions = spi_decode_words(clk, mosi, miso, bits=8)
    assert len(regions) == 1
    assert regions[0].mosi == [0x9F]
    assert regions[0].clock_edges == 8
    assert spi_decode_words(clk, mosi, miso, bits=12)[0].mosi == []


def test_miso_defaults_to_zero_without_a_line():
    clk, mosi, _, cs = waveform([[(0x81, 0xFF)]], 8)
    assert spi_decode_words(clk, mosi, None, cs)[0].miso == [0]


def test_decode_rejects_an_empty_word():
    with pytest.raises(ValueError):
        spi_decode_words([0, 1], [0, 0], bits=0)


@pytest.mark.parametrize(
    ("data", "bits", "frames"),
    [
        (b"\x12\xff\x00", 8, [0x12, 0xFF, 0x00]),
        (b"\x12\xff\x00", 4, [0x2, 0xF, 0x0]),
        (b"\xa5", 7, [0x25]),
        (b"\x34\x12\xff\xff", 16, [0x1234, 0xFFFF]),
        (b"\x34\x12\xff\xff", 12, [0x234, 0xFFF]),
        (b"\xff\x01", 9, [0x1FF]),
        (b"", 16, []),
    ],
)
def test_frames(data, bits, frames):
    assert spi_frames(data, bits) == frames


@pytest.mark.parametrize("bits", [9, 12, 16])
def test_wide_frames_take_byte_pairs(bits):
    with pytest.raises(ValueError):
        spi_frames(b"\x01\x02\x03", bits)


@pytest.mark.parametrize("bits", [BITS_MIN - 1, BITS_MAX + 1])
def test_frame_sizes_outside_the_driver_range(bits):
    with pytest.raises(ValueError):
        spi_frames(b"\x00\x00", bits)
    with pytest.raises(ValueError):
        spi_bytes([0], bits)


@pytest.mark.parametrize("bits", range(BITS_MIN, BITS_MAX + 1))
def test_bytes_invert_frames(bits):
    data = bytes((i * 37 + 11) & 0xFF for i in range(8))
    frames = spi_frames(data, bits)
    assert spi_frames(spi_bytes(frames, bits), bits) == frames
    assert len(spi_bytes(frames, bits)) == len(data)


def test_received_frames_are_right_aligned():
    assert spi_bytes([0xF, 0x3], 4) == b"\x0f\x03"
    assert spi_bytes([0xFFF], 12) == b"\xff\x0f"
    assert spi_bytes([0x1FFFF], 16) == b"\xff\xff"
