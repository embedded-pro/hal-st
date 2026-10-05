import pytest

from hal_st_validation.patterns import PATTERNS, crc32, crc_text, generate


def test_reference_vectors():
    """The C.8 vectors the firmware's `Payload.cpp` is checked against."""
    assert generate(4, "inc", 0xFE) == bytes.fromhex("feff0001")
    assert generate(8, "prbs", 1) == bytes.fromhex("2101c54fd1d01ab2")
    assert crc_text(generate(256)) == "29058c73"
    assert crc_text(generate(1024, "prbs", 1)) == "696138f0"


def test_patterns():
    assert generate(3, "const", 0x1A5) == b"\xa5\xa5\xa5"
    assert generate(300)[255:258] == b"\xff\x00\x01"
    assert generate(0, "prbs") == b""
    assert generate(8, "prbs", 0) == generate(8, "prbs", 1), "seed 0 starts the generator at 1"
    assert generate(16, "prbs", 0xFFFFFFFF) != generate(16, "prbs", 1)
    assert generate(64, "prbs", 7)[:10] == generate(10, "prbs", 7), "a prefix does not depend on the length"
    assert set(PATTERNS) == {"inc", "const", "prbs"}


def test_prbs_is_xorshift32():
    state, expected = 0x12345678, bytearray()
    for _ in range(32):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        expected.append(state & 0xFF)
    assert generate(32, "prbs", 0x12345678) == bytes(expected)


def test_crc_is_the_firmware_crc32():
    assert crc32(b"123456789") == 0xCBF43926, "the CRC-32 check value"
    assert crc_text(b"") == "00000000"


@pytest.mark.parametrize(("length", "pattern", "seed"), [(-1, "inc", 0), (1, "inc", -1), (1, "inc", 1 << 32), (1, "noise", 0)])
def test_invalid_arguments(length, pattern, seed):
    with pytest.raises(ValueError):
        generate(length, pattern, seed)
