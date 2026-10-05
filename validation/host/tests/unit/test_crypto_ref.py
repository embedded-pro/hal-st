"""`crypto_ref`: AES-128 against FIPS-197 and SP 800-38A, the data swapping model of the STM32 AES peripheral, and
P-256 arithmetic against known multiples of G and the NIST CAVP ECC CDH vector."""

import pytest

from hal_st_validation import crypto_ref
from hal_st_validation.crypto_ref import P256

KEY = bytes(range(16))


@pytest.mark.parametrize("vector", [crypto_ref.AES_FIPS197_C1, crypto_ref.AES_SP800_38A_F11], ids=lambda vector: vector.name)
def test_aes_vectors(vector):
    assert crypto_ref.aes_ecb_encrypt(vector.key, vector.plaintext) == vector.ciphertext
    assert crypto_ref.aes_ecb_decrypt(vector.key, vector.ciphertext) == vector.plaintext


def test_fips197_c1_literal():
    key = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
    plaintext = bytes.fromhex("00112233445566778899aabbccddeeff")
    assert crypto_ref.aes_encrypt_block(key, plaintext).hex() == "69c4e0d86a7b0430d8cdb78070b4c55a"


def test_sbox_known_entries():
    """FIPS-197 Figure 7: S(00) = 63, S(53) = ed, S(ff) = 16; the inverse undoes it."""
    assert (crypto_ref.SBOX[0x00], crypto_ref.SBOX[0x53], crypto_ref.SBOX[0xFF]) == (0x63, 0xED, 0x16)
    assert all(crypto_ref.INVERSE_SBOX[crypto_ref.SBOX[value]] == value for value in range(256))


def test_ecb_needs_whole_blocks():
    with pytest.raises(ValueError):
        crypto_ref.aes_ecb_encrypt(KEY, bytes(15))
    with pytest.raises(ValueError):
        crypto_ref.aes_ecb_encrypt(bytes(15), bytes(16))


def test_swap_words():
    data = bytes([0x01, 0x02, 0x03, 0x04, 0x80, 0x40, 0x20, 0x10])
    assert crypto_ref.swap_words(data, "byte") == data
    assert crypto_ref.swap_words(data, "none") == bytes([0x04, 0x03, 0x02, 0x01, 0x10, 0x20, 0x40, 0x80])
    assert crypto_ref.swap_words(data, "half") == bytes([0x02, 0x01, 0x04, 0x03, 0x40, 0x80, 0x10, 0x20])
    assert crypto_ref.swap_words(data, "bit") == bytes([0x80, 0x40, 0xC0, 0x20, 0x01, 0x02, 0x04, 0x08])
    with pytest.raises(ValueError):
        crypto_ref.swap_words(bytes(3), "none")


@pytest.mark.parametrize("swap", crypto_ref.SWAPS)
def test_swap_is_an_involution_and_round_trips(swap):
    data = bytes(range(48))
    assert crypto_ref.swap_words(crypto_ref.swap_words(data, swap), swap) == data
    ciphertext = crypto_ref.aes_stm(KEY, data, swap)
    assert crypto_ref.aes_stm(KEY, ciphertext, swap, decrypt=True) == data


def test_byte_swap_is_standard_aes_and_the_others_differ():
    vector = crypto_ref.AES_FIPS197_C1
    assert crypto_ref.aes_stm(vector.key, vector.plaintext) == vector.ciphertext
    others = {crypto_ref.aes_stm(vector.key, vector.plaintext, swap) for swap in ("none", "half", "bit")}
    assert vector.ciphertext not in others and len(others) == 3


def test_none_swap_reverses_each_word_around_aes():
    vector = crypto_ref.AES_FIPS197_C1
    swapped = crypto_ref.swap_words(vector.plaintext, "none")
    assert crypto_ref.aes_stm(vector.key, swapped, "none") == crypto_ref.swap_words(vector.ciphertext, "none")


def test_generator_on_curve():
    assert P256.on_curve(P256.g)
    assert not P256.on_curve((P256.gx, P256.gy + 1))
    assert not P256.on_curve((P256.p, 0))
    assert P256.on_curve(None)


@pytest.mark.parametrize(
    ("k", "x", "y"),
    [
        (
            1,
            0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
            0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5,
        ),
        (
            2,
            0x7CF27B188D034F7E8A52380304B51AC3C08969E277F21B35A60B48FC47669978,
            0x07775510DB8ED040293D9AC69F7430DBBA7DADE63CE982299E04B79D227873D1,
        ),
        (
            3,
            0x5ECBE4D1A6330A44C8F7EF951D4BF165E6C6B721EFADA985FB41661BC6E7FD6C,
            0x8734640C4998FF7E374B06CE1A64A2ECD82AB036384FB83D9A79B127A27D5032,
        ),
    ],
)
def test_multiples_of_g(k, x, y):
    assert crypto_ref.scalar_multiply(P256, k) == (x, y)


def test_group_order():
    assert crypto_ref.scalar_multiply(P256, P256.n) is None
    assert crypto_ref.scalar_multiply(P256, P256.n - 1) == (P256.gx, P256.p - P256.gy)
    assert crypto_ref.scalar_multiply(P256, 0) is None


def test_addition_rules():
    double = crypto_ref.point_double(P256, P256.g)
    assert crypto_ref.point_add(P256, P256.g, P256.g) == double
    assert crypto_ref.point_add(P256, None, P256.g) == P256.g
    assert crypto_ref.point_add(P256, P256.g, None) == P256.g
    assert crypto_ref.point_add(P256, P256.g, (P256.gx, P256.p - P256.gy)) is None
    assert crypto_ref.point_add(P256, double, P256.g) == crypto_ref.scalar_multiply(P256, 3)


def test_cavp_cdh_vector():
    vector = crypto_ref.P256_CAVP_CDH
    assert crypto_ref.scalar_multiply(P256, vector.d) == (vector.qx, vector.qy)
    shared = crypto_ref.scalar_multiply(P256, vector.d, (vector.peer_x, vector.peer_y))
    assert shared is not None and shared[0] == vector.z
    assert P256.on_curve((vector.peer_x, vector.peer_y)) and P256.on_curve(shared)


def test_bytes_conversion():
    assert crypto_ref.to_bytes(1) == bytes(31) + b"\x01"
    assert crypto_ref.from_bytes(b"\x01\x00") == 256
    assert crypto_ref.to_bytes(0x0102, 4) == b"\x00\x00\x01\x02"
