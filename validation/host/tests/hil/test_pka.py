"""Public key accelerator (`hal::PkaStm` on `services::secp256r1`) through the `pka` group: scalar multiplication
(multiples of G, the CAVP ECC CDH vector, n - 1, operands shorter than 32 bytes padded by the firmware), the point
check on and off the curve, the comparison of 4-60 byte operands and the duration of one multiplication. Results
are checked against `crypto_ref`'s affine P-256 arithmetic. No wiring.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.crypto_ref import P256, P256_CAVP_CDH, from_bytes, scalar_multiply, to_bytes


@pytest.fixture
def pka_cfg(board_cfg):
    return board_cfg.param("pka")


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def as_point(product):
    return from_bytes(product.x), from_bytes(product.y)


def test_default_is_g(fw):
    """Without operands the firmware multiplies G by 1."""
    assert as_point(fw.pka.mul()) == P256.g


@pytest.mark.board_params("multiple", "pka.multiples")
def test_multiple_of_g(fw, multiple):
    k = bytes.fromhex(multiple["k"])
    expected = (int(multiple["x"], 16), int(multiple["y"], 16))
    assert scalar_multiply(P256, from_bytes(k)) == expected, "the vector itself"
    product = fw.pka.mul(k=k)
    assert len(product.x) == len(product.y) == 32
    assert as_point(product) == expected


def test_short_scalar_is_padded(fw):
    """A 1-byte scalar equals the same value in 32 bytes: the firmware left-pads every operand."""
    assert as_point(fw.pka.mul(k=b"\x05")) == as_point(fw.pka.mul(k=to_bytes(5))) == scalar_multiply(P256, 5)


def test_order_minus_one(fw):
    assert as_point(fw.pka.mul(k=to_bytes(P256.n - 1))) == (P256.gx, P256.p - P256.gy)


def test_cavp_cdh(fw):
    """NIST CAVP ECC CDH (P-256): d * G is the public key, d * Q has the shared secret as x coordinate."""
    vector = P256_CAVP_CDH
    public = fw.pka.mul(k=to_bytes(vector.d))
    assert as_point(public) == (vector.qx, vector.qy)
    shared = fw.pka.mul(k=to_bytes(vector.d), x=to_bytes(vector.peer_x), y=to_bytes(vector.peer_y))
    assert from_bytes(shared.x) == vector.z
    assert as_point(shared) == scalar_multiply(P256, vector.d, (vector.peer_x, vector.peer_y))


def test_given_point(fw):
    """`x`/`y` replace G: 3 * (2G) = 6G."""
    double = scalar_multiply(P256, 2)
    assert double is not None
    product = fw.pka.mul(k=b"\x03", x=to_bytes(double[0]), y=to_bytes(double[1]))
    assert as_point(product) == scalar_multiply(P256, 6)


def test_point_check(fw):
    double = scalar_multiply(P256, 2)
    assert double is not None
    assert fw.pka.check(to_bytes(P256.gx), to_bytes(P256.gy))
    assert fw.pka.check(to_bytes(double[0]), to_bytes(double[1]))
    assert not fw.pka.check(to_bytes(P256.gx), to_bytes(P256.gy + 1))
    assert not fw.pka.check(b"\x01", b"\x02")


@pytest.mark.board_params("length", "pka.compare_lengths")
def test_compare(fw, length):
    low = bytes(length - 1) + b"\x01"
    high = b"\x01" + bytes(length - 1)
    assert fw.pka.cmp(low, high) == "lt"
    assert fw.pka.cmp(high, low) == "gt"
    assert fw.pka.cmp(high, high) == "eq"
    assert fw.pka.cmp(low, low[:-1] + b"\x02") == "lt"


def test_duration(fw, pka_cfg):
    """One multiplication with a full 256-bit scalar is timed by the firmware (`us`)."""
    product = fw.pka.mul(k=to_bytes(P256.n - 2))
    assert 0 < product.us <= pka_cfg["max_us"], product.us


OPERAND_33 = "01" * 33


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("pka.mul 1", "usage"),
        ("pka.mul z=01", "usage"),
        ("pka.mul x=01", "usage"),
        ("pka.mul y=01", "usage"),
        ("pka.mul k=1", "usage"),
        ("pka.mul k=-", "usage"),
        ("pka.mul k=zz", "usage"),
        ("pka.mul k=01 x=01 y=0", "usage"),
        (f"pka.mul k={OPERAND_33}", "range"),
        (f"pka.mul x={OPERAND_33} y=01", "range"),
        ("pka.check", "usage"),
        ("pka.check x=01", "usage"),
        ("pka.check x=01 y=02 k=01", "usage"),
        (f"pka.check x=01 y={OPERAND_33}", "range"),
        ("pka.cmp a=00000001", "usage"),
        ("pka.cmp a=00000001 b=0000000001", "usage"),
        ("pka.cmp a=0000000g b=00000001", "usage"),
        ("pka.cmp a=000001 b=000002", "range"),
    ],
)
def test_errors(fw, line, reason):
    expect_reason(fw, line, reason)
