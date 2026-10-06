"""AES-128 ECB (`hal::SynchronousAes128EcbStm`) through the `aes` group: the FIPS-197 C.1 and SP 800-38A F.1.1
known answers, decryption, one to five blocks per command, key changes between commands, and every data swapping mode
(`swap=none|half|byte|bit`, AES_CR.DATATYPE) against the model of `crypto_ref.aes_stm`. No wiring.

Scenarios: features/aes.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

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
@scenario("aes.feature", "The firmware gives the known answers")
def test_known_answer(vector):
    pass


@pytest.mark.board_params("vector", "aes.vectors")
@scenario("aes.feature", "Each block on its own gives the same as all blocks at once")
def test_block_by_block(vector):
    pass


@pytest.mark.board_params("length", "aes.lengths")
@scenario("aes.feature", "Encrypted data decrypts to itself")
def test_round_trip(length):
    pass


@scenario("aes.feature", "The key of one command does not leak into the next")
def test_key_change():
    pass


@pytest.mark.board_params("swap", "aes.swaps")
@scenario("aes.feature", "Encryption and decryption swap the data as AES_CR.DATATYPE says")
def test_swap(swap):
    pass


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
@scenario("aes.feature", "Malformed commands are refused")
def test_errors(line, reason):
    pass


@given("the key, plaintext and ciphertext of the vector", target_fixture="vector_bytes")
def vector_bytes(vector):
    return unpack(vector)


@given(parsers.parse("the key is {size:d} PRBS bytes with seed {seed:d}"), target_fixture="key")
def prbs_key(size, seed):
    return patterns.generate(size, "prbs", seed)


@given(parsers.parse("the first key is {size:d} PRBS bytes with seed {seed:d}"), target_fixture="first_key")
def first_prbs_key(size, seed):
    return patterns.generate(size, "prbs", seed)


@given(parsers.parse("the second key is {size:d} PRBS bytes with seed {seed:d}"), target_fixture="second_key")
def second_prbs_key(size, seed):
    return patterns.generate(size, "prbs", seed)


@given("the data is the length of PRBS bytes seeded with the length", target_fixture="data")
def prbs_data_of_length(length):
    return patterns.generate(length, "prbs", length)


@given(parsers.parse("the data is {size:d} PRBS bytes with seed {seed:d}"), target_fixture="data")
def prbs_data(size, seed):
    return patterns.generate(size, "prbs", seed)


@given(parsers.parse("the data is {size:d} incrementing bytes from {start:d}"), target_fixture="data")
def incrementing_data(size, start):
    return patterns.generate(size, "inc", start)


@when("the firmware encrypts the data", target_fixture="ciphertext")
def encrypt(fw, key, data):
    return fw.aes.enc(key, data)


@when("the firmware encrypts the data with the swap", target_fixture="ciphertext")
def encrypt_with_swap(fw, key, data, swap):
    return fw.aes.enc(key, data, swap=swap)


@when("the firmware encrypts the data with the first key and then with the second key", target_fixture="ciphertexts")
def encrypt_with_both_keys(fw, data, first_key, second_key):
    with_first = fw.aes.enc(first_key, data)
    with_second = fw.aes.enc(second_key, data)
    return with_first, with_second


@then("the reference model encrypts the plaintext to the ciphertext")
def reference_known_answer(vector_bytes):
    key, plaintext, ciphertext = vector_bytes
    assert crypto_ref.aes_ecb_encrypt(key, plaintext) == ciphertext, "the vector itself"


@then("the firmware encrypts the plaintext to the ciphertext")
def firmware_known_answer(fw, vector_bytes):
    key, plaintext, ciphertext = vector_bytes
    assert fw.aes.enc(key, plaintext) == ciphertext


@then(parsers.parse('the firmware encrypts the plaintext to the ciphertext with swap "{default_swap}", the default'))
def firmware_known_answer_default_swap(fw, vector_bytes, default_swap):
    key, plaintext, ciphertext = vector_bytes
    assert fw.aes.enc(key, plaintext, swap=default_swap) == ciphertext, "swap=byte is the default"


@then("the firmware decrypts the ciphertext to the plaintext")
def firmware_known_decryption(fw, vector_bytes):
    key, plaintext, ciphertext = vector_bytes
    assert fw.aes.dec(key, ciphertext) == plaintext


@then("every 16-byte block of the plaintext, encrypted on its own, gives its block of the ciphertext")
def block_by_block(fw, vector_bytes):
    key, plaintext, ciphertext = vector_bytes
    for offset in range(0, len(plaintext), BLOCK):
        assert fw.aes.enc(key, plaintext[offset : offset + BLOCK]) == ciphertext[offset : offset + BLOCK], offset


@then("the ciphertext is the reference model's encryption of the data")
def reference_encryption(key, data, ciphertext):
    assert ciphertext == crypto_ref.aes_ecb_encrypt(key, data)


@then("the firmware decrypts the ciphertext to the data")
def decrypts_to_data(fw, key, data, ciphertext):
    assert fw.aes.dec(key, ciphertext) == data


@then("each ciphertext is the reference model's encryption of the data with its key")
def reference_encryption_per_key(data, first_key, second_key, ciphertexts):
    with_first, with_second = ciphertexts
    assert with_first == crypto_ref.aes_ecb_encrypt(first_key, data)
    assert with_second == crypto_ref.aes_ecb_encrypt(second_key, data)


@then("the two ciphertexts differ")
def ciphertexts_differ(ciphertexts):
    with_first, with_second = ciphertexts
    assert with_first != with_second


@then("the firmware encrypts the data with the first key again to the first ciphertext")
def first_key_again(fw, data, first_key, ciphertexts):
    assert fw.aes.enc(first_key, data) == ciphertexts[0]


@then("the firmware decrypts the second ciphertext with the second key to the data")
def second_key_decrypts(fw, data, second_key, ciphertexts):
    assert fw.aes.dec(second_key, ciphertexts[1]) == data


@then("the ciphertext is the swapping model's encryption of the data with the swap")
def swapping_model_encryption(key, data, swap, ciphertext):
    assert ciphertext == crypto_ref.aes_stm(key, data, swap), swap


@then("the firmware decrypts the ciphertext with the swap to the data")
def decrypts_with_swap(fw, key, data, swap, ciphertext):
    assert fw.aes.dec(key, ciphertext, swap=swap) == data


@then("the firmware decrypts the data with the swap to the swapping model's decryption of the data")
def swapping_model_decryption(fw, key, data, swap):
    assert fw.aes.dec(key, data, swap=swap) == crypto_ref.aes_stm(key, data, swap, decrypt=True)


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
