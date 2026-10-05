"""AES-128 ECB (`hal::SynchronousAes128EcbStm`) through the `aes` group: the FIPS-197 C.1 and SP 800-38A F.1.1
known answers, decryption, one to five blocks per command, key changes between commands, and every data swapping mode
(`swap=none|half|byte|bit`, AES_CR.DATATYPE) against the model of `crypto_ref.aes_stm`. No wiring.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import crypto_ref, patterns

BLOCK = 16


@pytest.fixture
def aes_cfg(board_cfg):
    return board_cfg.param("aes")


def hex_bytes(value):
    """A hex string, or a list of them (the board file splits long vectors into blocks)."""
    return bytes.fromhex(value if isinstance(value, str) else "".join(value))


def unpack(vector):
    return hex_bytes(vector["key"]), hex_bytes(vector["plaintext"]), hex_bytes(vector["ciphertext"])


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


@pytest.mark.board_params("vector", "aes.vectors")
def test_known_answer(fw, vector):
    key, plaintext, ciphertext = unpack(vector)
    assert crypto_ref.aes_ecb_encrypt(key, plaintext) == ciphertext, "the vector itself"
    assert fw.aes.enc(key, plaintext) == ciphertext
    assert fw.aes.enc(key, plaintext, swap="byte") == ciphertext, "swap=byte is the default"
    assert fw.aes.dec(key, ciphertext) == plaintext


@pytest.mark.board_params("vector", "aes.vectors")
def test_block_by_block(fw, vector):
    """ECB: each block on its own gives the same as all blocks in one command."""
    key, plaintext, ciphertext = unpack(vector)
    for offset in range(0, len(plaintext), BLOCK):
        assert fw.aes.enc(key, plaintext[offset : offset + BLOCK]) == ciphertext[offset : offset + BLOCK], offset


@pytest.mark.board_params("length", "aes.lengths")
def test_round_trip(fw, length):
    key = patterns.generate(BLOCK, "prbs", 7)
    data = patterns.generate(length, "prbs", length)
    ciphertext = fw.aes.enc(key, data)
    assert ciphertext == crypto_ref.aes_ecb_encrypt(key, data)
    assert fw.aes.dec(key, ciphertext) == data


def test_key_change(fw):
    """The key of one command does not leak into the next."""
    data = patterns.generate(2 * BLOCK, "inc", 0)
    first = patterns.generate(BLOCK, "prbs", 1)
    second = patterns.generate(BLOCK, "prbs", 2)
    with_first = fw.aes.enc(first, data)
    with_second = fw.aes.enc(second, data)
    assert with_first == crypto_ref.aes_ecb_encrypt(first, data)
    assert with_second == crypto_ref.aes_ecb_encrypt(second, data)
    assert with_first != with_second
    assert fw.aes.enc(first, data) == with_first
    assert fw.aes.dec(second, with_second) == data


@pytest.mark.board_params("swap", "aes.swaps")
def test_swap(fw, swap):
    """Encryption and decryption with the data swapping of AES_CR.DATATYPE (model: `crypto_ref.aes_stm`)."""
    key = patterns.generate(BLOCK, "prbs", 3)
    data = patterns.generate(3 * BLOCK, "prbs", 4)
    ciphertext = fw.aes.enc(key, data, swap=swap)
    assert ciphertext == crypto_ref.aes_stm(key, data, swap), swap
    assert fw.aes.dec(key, ciphertext, swap=swap) == data
    assert fw.aes.dec(key, data, swap=swap) == crypto_ref.aes_stm(key, data, swap, decrypt=True)


KEY = "000102030405060708090a0b0c0d0e0f"
DATA = "00112233445566778899aabbccddeeff"


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("aes.enc", "usage"),
        (f"aes.enc {KEY}", "usage"),
        (f"aes.enc {KEY} {DATA} {DATA}", "usage"),
        (f"aes.enc {KEY} {DATA} mode=cbc", "usage"),
        (f"aes.enc {KEY[:-2]} {DATA}", "usage"),
        (f"aes.enc {KEY}00 {DATA}", "usage"),
        (f"aes.enc {KEY}{KEY} {DATA}", "usage"),
        (f"aes.enc - {DATA}", "usage"),
        (f"aes.enc {KEY} -", "usage"),
        (f"aes.enc {KEY} {DATA[:-2]}", "usage"),
        (f"aes.enc {KEY} {DATA}00", "usage"),
        (f"aes.enc {KEY} {DATA[:-1]}", "usage"),
        (f"aes.enc {KEY} {DATA[:-2]}zz", "usage"),
        (f"aes.enc {KEY} {DATA * 6}", "usage"),
        (f"aes.enc {KEY} {DATA} swap=word", "usage"),
        (f"aes.dec {KEY} {DATA * 6}", "usage"),
        (f"aes.dec {KEY} {DATA} swap=nibble", "usage"),
    ],
)
def test_errors(fw, line, reason):
    expect_reason(fw, line, reason)
