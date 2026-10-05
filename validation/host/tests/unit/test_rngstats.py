"""`rngstats`: the `rng.stats` counts (most significant bit first, chi-square times 1000 in integer arithmetic,
zlib CRC-32) and the monobit, chi-square and runs bounds."""

import math
import random
import zlib

import pytest

from hal_st_validation import patterns, rngstats


def test_counts_of_small_streams():
    stats = rngstats.byte_stats(b"\x00\xff")
    assert (stats.n, stats.ones, stats.runs) == (2, 8, 2)
    assert rngstats.byte_stats(b"\x55").runs == 8
    assert rngstats.byte_stats(b"\x80\x01").runs == 3
    assert rngstats.byte_stats(b"\x01\x80").runs == 3
    assert rngstats.byte_stats(b"\x01").runs == 2
    assert rngstats.byte_stats(b"\x00").runs == 1


def test_counts_match_a_bitwise_walk():
    data = bytes(random.Random(5).getrandbits(8) for _ in range(300))
    bits = [(byte >> (7 - position)) & 1 for byte in data for position in range(8)]
    stats = rngstats.byte_stats(data)
    assert stats.ones == sum(bits)
    assert stats.runs == 1 + sum(1 for a, b in zip(bits, bits[1:]) if a != b)
    assert stats.crc == zlib.crc32(data)


def test_chi_square():
    """A flat histogram is 0; all bytes alike is 255 n; the integer form truncates sum((c - E)^2 / E) * 1000."""
    assert rngstats.byte_stats(bytes(range(256)) * 4).chisq_x1000 == 0
    assert rngstats.byte_stats(bytes(64)).chisq_x1000 == 255 * 64 * 1000
    data = bytes(random.Random(1).getrandbits(8) for _ in range(4096))
    expected = 4096 / 256
    exact = sum((data.count(value) - expected) ** 2 / expected for value in range(256))
    assert rngstats.byte_stats(data).chisq_x1000 == math.floor(exact * 1000)


def test_chi_square_saturates_like_the_firmware():
    assert rngstats.byte_stats(bytes(65536)).chisq_x1000 == 0xFFFFFFFF


def test_random_data_passes():
    for seed in (1, 2, 3):
        data = patterns.generate(65536, "prbs", seed)
        stats = rngstats.byte_stats(data)
        assert rngstats.failures(stats.n, stats.ones, stats.runs, stats.chisq_x1000) == [], seed


@pytest.mark.parametrize(
    ("data", "failed"),
    [
        (bytes([0x0F, 0xFF]) * 4096, "monobit"),
        (bytes([0x55, 0xAA]) * 4096, "chi-square"),
        (bytes([0x0F]) * 8192, "runs"),
    ],
)
def test_biased_data_fails(data, failed):
    stats = rngstats.byte_stats(data)
    assert any(message.startswith(failed) for message in rngstats.failures(stats.n, stats.ones, stats.runs, stats.chisq_x1000))


def test_bounds():
    bits = 8 * 65536
    sigma = math.sqrt(bits) / 2
    assert rngstats.monobit_ok(65536, bits // 2 + int(3.9 * sigma))
    assert not rngstats.monobit_ok(65536, bits // 2 + int(4.1 * sigma))
    assert rngstats.chi_square_ok(330499)
    assert not rngstats.chi_square_ok(330500)
    assert rngstats.runs_ok(65536, bits // 2, bits // 2)
    assert not rngstats.runs_ok(65536, bits // 2, bits // 2 + int(4.1 * math.sqrt(bits) / 2))
    assert not rngstats.runs_ok(65536, bits // 2 + bits // 100, bits // 2)
