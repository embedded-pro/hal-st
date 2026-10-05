"""Reference models of the `aes` and `pka` groups (validation/PROTOCOL.md): pure-Python AES-128 (FIPS-197) with the
data swapping of the STM32 AES peripheral as `hal::SynchronousAes128EcbStm` uses it, and affine arithmetic on the
NIST P-256 curve (`services::secp256r1`).

Data swapping (RM0434 / RM0493, AES_CR.DATATYPE): the driver writes each 32-bit word of the input to AES_DINR as the
little-endian CPU loads it, the first word being bits 127:96 of the block, and stores AES_DOUTR the same way; the
peripheral swaps the halves (`half`), the bytes (`byte`) or the bits (`bit`) of every word on the way in and out.
So the result is `T(AES(T(data)))` with T per 4-byte word: `none` reverses the bytes, `half` gives b1 b0 b3 b2,
`byte` is the identity (standard AES on the byte string, the driver's default) and `bit` reverses the bits of every
byte. The key is unaffected: the driver writes it byte-reversed per word (`__REV`), i.e. in standard order.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Literal

__all__ = [
    "AES_FIPS197_C1",
    "AES_SP800_38A_F11",
    "INVERSE_SBOX",
    "P256",
    "P256_CAVP_CDH",
    "SBOX",
    "SWAPS",
    "AesVector",
    "CdhVector",
    "Curve",
    "Point",
    "Swap",
    "aes_decrypt_block",
    "aes_ecb_decrypt",
    "aes_ecb_encrypt",
    "aes_encrypt_block",
    "aes_stm",
    "from_bytes",
    "point_add",
    "point_double",
    "scalar_multiply",
    "swap_words",
    "to_bytes",
]

Swap = Literal["none", "half", "byte", "bit"]
SWAPS: tuple[Swap, ...] = ("none", "half", "byte", "bit")

BLOCK = 16


def _multiply(a: int, b: int) -> int:
    """Product in GF(2^8) modulo x^8 + x^4 + x^3 + x + 1."""
    result = 0
    while b:
        if b & 1:
            result ^= a
        a = ((a << 1) ^ 0x11B) if a & 0x80 else a << 1
        b >>= 1
    return result


def _power(a: int, exponent: int) -> int:
    result = 1
    while exponent:
        if exponent & 1:
            result = _multiply(result, a)
        a = _multiply(a, a)
        exponent >>= 1
    return result


def _sbox() -> tuple[tuple[int, ...], tuple[int, ...]]:
    """FIPS-197 5.1.1: the multiplicative inverse followed by the affine transformation."""
    inverse = [0] + [_power(value, 254) for value in range(1, 256)]
    forward = []
    for value in inverse:
        result = 0x63
        for shift in range(5):
            result ^= ((value << shift) | (value >> (8 - shift))) & 0xFF
        forward.append(result)
    backward = [0] * 256
    for index, value in enumerate(forward):
        backward[value] = index
    return tuple(forward), tuple(backward)


SBOX, INVERSE_SBOX = _sbox()


def _expand_key(key: bytes) -> list[list[int]]:
    """FIPS-197 5.2: eleven round keys of 16 bytes for AES-128."""
    if len(key) != BLOCK:
        raise ValueError("AES-128 takes a 16-byte key")
    words = [list(key[i : i + 4]) for i in range(0, BLOCK, 4)]
    rcon = 1
    for index in range(4, 44):
        word = list(words[index - 1])
        if index % 4 == 0:
            word = [SBOX[byte] for byte in word[1:] + word[:1]]
            word[0] ^= rcon
            rcon = _multiply(rcon, 2)
        words.append([a ^ b for a, b in zip(words[index - 4], word)])
    return [sum(words[round_ * 4 : round_ * 4 + 4], []) for round_ in range(11)]


def _add(state: list[int], round_key: list[int]) -> list[int]:
    return [a ^ b for a, b in zip(state, round_key)]


def _shift_rows(state: list[int], direction: int) -> list[int]:
    """The state is column major (byte r + 4c is row r, column c); row r rotates left by `direction * r`."""
    return [state[(row + 4 * (column + direction * row)) % 16] for column in range(4) for row in range(4)]


def _mix_columns(state: list[int], matrix: tuple[int, int, int, int]) -> list[int]:
    result = []
    for column in range(4):
        values = state[4 * column : 4 * column + 4]
        for row in range(4):
            byte = 0
            for k in range(4):
                byte ^= _multiply(matrix[(k - row) % 4], values[k])
            result.append(byte)
    return result


def aes_encrypt_block(key: bytes, block: bytes) -> bytes:
    keys = _expand_key(key)
    state = _add(list(block), keys[0])
    for round_ in range(1, 11):
        state = _shift_rows([SBOX[byte] for byte in state], 1)
        if round_ != 10:
            state = _mix_columns(state, (2, 3, 1, 1))
        state = _add(state, keys[round_])
    return bytes(state)


def aes_decrypt_block(key: bytes, block: bytes) -> bytes:
    keys = _expand_key(key)
    state = _add(list(block), keys[10])
    for round_ in range(9, -1, -1):
        state = [INVERSE_SBOX[byte] for byte in _shift_rows(state, -1)]
        state = _add(state, keys[round_])
        if round_ != 0:
            state = _mix_columns(state, (14, 11, 13, 9))
    return bytes(state)


def _ecb(function: Callable[[bytes, bytes], bytes], key: bytes, data: bytes) -> bytes:
    if len(data) % BLOCK:
        raise ValueError("ECB data must be a multiple of 16 bytes")
    return b"".join(function(key, data[i : i + BLOCK]) for i in range(0, len(data), BLOCK))


def aes_ecb_encrypt(key: bytes, data: bytes) -> bytes:
    return _ecb(aes_encrypt_block, key, data)


def aes_ecb_decrypt(key: bytes, data: bytes) -> bytes:
    return _ecb(aes_decrypt_block, key, data)


def _reverse_bits(byte: int) -> int:
    return int(f"{byte:08b}"[::-1], 2)


def swap_words(data: bytes, swap: Swap) -> bytes:
    """The memory image of the 128-bit block the AES core sees (or the inverse: every T is an involution)."""
    if len(data) % 4:
        raise ValueError("data must be whole 32-bit words")
    if swap == "byte":
        return bytes(data)
    if swap == "bit":
        return bytes(_reverse_bits(byte) for byte in data)
    order = {"none": (3, 2, 1, 0), "half": (1, 0, 3, 2)}[swap]
    return b"".join(bytes(data[i + j] for j in order) for i in range(0, len(data), 4))


def aes_stm(key: bytes, data: bytes, swap: Swap = "byte", decrypt: bool = False) -> bytes:
    """What `aes.enc`/`aes.dec` return for `swap`."""
    function = aes_ecb_decrypt if decrypt else aes_ecb_encrypt
    return swap_words(function(key, swap_words(data, swap)), swap)


@dataclass(frozen=True)
class AesVector:
    name: str
    key: bytes
    plaintext: bytes
    ciphertext: bytes


# FIPS-197 Appendix C.1 (AES-128).
AES_FIPS197_C1 = AesVector(
    "fips197-c1",
    bytes.fromhex("000102030405060708090a0b0c0d0e0f"),
    bytes.fromhex("00112233445566778899aabbccddeeff"),
    bytes.fromhex("69c4e0d86a7b0430d8cdb78070b4c55a"),
)

# NIST SP 800-38A F.1.1 (ECB-AES128.Encrypt), all four blocks.
AES_SP800_38A_F11 = AesVector(
    "sp800-38a-f11",
    bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c"),
    bytes.fromhex(
        "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710"
    ),
    bytes.fromhex(
        "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf43b1cd7f598ece23881b00e3ed0306887b0c785e27e8ad3f8223207104725dd4"
    ),
)

Point = tuple[int, int] | None


@dataclass(frozen=True)
class Curve:
    """y^2 = x^3 + ax + b over GF(p), base point (gx, gy) of order n."""

    p: int
    a: int
    b: int
    gx: int
    gy: int
    n: int

    @property
    def g(self) -> tuple[int, int]:
        return (self.gx, self.gy)

    def on_curve(self, point: Point) -> bool:
        if point is None:
            return True
        x, y = point
        return 0 <= x < self.p and 0 <= y < self.p and (y * y - x * x * x - self.a * x - self.b) % self.p == 0


# FIPS 186-4 D.1.2.3 (= services::secp256r1).
P256 = Curve(
    p=0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF,
    a=0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC,
    b=0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B,
    gx=0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
    gy=0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5,
    n=0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551,
)


def point_double(curve: Curve, point: Point) -> Point:
    if point is None or point[1] == 0:
        return None
    x, y = point
    slope = (3 * x * x + curve.a) * pow(2 * y, -1, curve.p) % curve.p
    rx = (slope * slope - 2 * x) % curve.p
    return rx, (slope * (x - rx) - y) % curve.p


def point_add(curve: Curve, first: Point, second: Point) -> Point:
    if first is None:
        return second
    if second is None:
        return first
    (x1, y1), (x2, y2) = first, second
    if x1 == x2:
        return point_double(curve, first) if (y1 + y2) % curve.p else None
    slope = (y2 - y1) * pow(x2 - x1, -1, curve.p) % curve.p
    rx = (slope * slope - x1 - x2) % curve.p
    return rx, (slope * (x1 - rx) - y1) % curve.p


def scalar_multiply(curve: Curve, k: int, point: Point | None = None) -> Point:
    """k * point (default the base point) by double-and-add from the most significant bit; None is the point at
    infinity."""
    base = curve.g if point is None else point
    result: Point = None
    for bit in range(k.bit_length() - 1, -1, -1):
        result = point_double(curve, result)
        if (k >> bit) & 1:
            result = point_add(curve, result, base)
    return result


def to_bytes(value: int, length: int = 32) -> bytes:
    return value.to_bytes(length, "big")


def from_bytes(data: bytes) -> int:
    return int.from_bytes(data, "big")


@dataclass(frozen=True)
class CdhVector:
    """NIST CAVP ECC CDH primitive test (KAS_ECC_CDH_PrimitiveTest, P-256 COUNT 0): `d * Q` has the x coordinate
    `z`, and `d * G` is the public key (`qx`, `qy`)."""

    d: int
    peer_x: int
    peer_y: int
    qx: int
    qy: int
    z: int


P256_CAVP_CDH = CdhVector(
    d=0x7D7DC5F71EB29DDAF80D6214632EEAE03D9058AF1FB6D22ED80BADB62BC1A534,
    peer_x=0x700C48F77F56584C5CC632CA65640DB91B6BACCE3A4DF6B42CE7CC838833D287,
    peer_y=0xDB71E509E3FD9B060DDB20BA5C51DCC5948D46FBF640DFE0441782CAB85FA4AC,
    qx=0xEAD218590119E8876B29146FF89CA61770C4EDBBF97D38CE385ED281D8A6B230,
    qy=0x28AF61281FD35E2FA7002523ACC85A429CB06EE6648325389F59EDFCE1405141,
    z=0x46FC62106420FF012E54A434FBDD2D25CCC5852060561E68040DD7778997BD7B,
)
