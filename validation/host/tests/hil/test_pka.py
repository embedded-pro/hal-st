"""Public key accelerator (`hal::PkaStm` on `services::secp256r1`) through the `pka` group: scalar multiplication
(multiples of G, the CAVP ECC CDH vector, n - 1, operands shorter than 32 bytes padded by the firmware), the point
check on and off the curve, the comparison of 4-60 byte operands and the duration of one multiplication. Results
are checked against `crypto_ref`'s affine P-256 arithmetic. No wiring.

Scenarios: features/pka.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

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


@scenario("pka.feature", "Without operands the firmware multiplies G by 1")
def test_default_is_g():
    pass


@pytest.mark.board_params("multiple", "pka.multiples")
@scenario("pka.feature", "The firmware multiplies G by the scalar of the multiple")
def test_multiple_of_g(multiple):
    pass


@scenario("pka.feature", "A short scalar is padded")
def test_short_scalar_is_padded():
    pass


@scenario("pka.feature", "G multiplied by n - 1 is the negation of G")
def test_order_minus_one():
    pass


@scenario("pka.feature", "The firmware passes the NIST CAVP ECC CDH vector")
def test_cavp_cdh():
    pass


@scenario("pka.feature", "The given point replaces G")
def test_given_point():
    pass


@scenario("pka.feature", "The point check accepts the points on the curve and rejects the others")
def test_point_check():
    pass


@pytest.mark.board_params("length", "pka.compare_lengths")
@scenario("pka.feature", "The comparison orders operands of the length")
def test_compare(length):
    pass


@scenario("pka.feature", "The firmware times a multiplication")
def test_duration():
    pass


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
@scenario("pka.feature", "Malformed and out-of-range commands are refused")
def test_errors(line, reason):
    pass


@given("the scalar and the expected point of the multiple", target_fixture="expected_multiple")
def expected_multiple(multiple):
    k = bytes.fromhex(multiple["k"])
    expected = (int(multiple["x"], 16), int(multiple["y"], 16))
    return k, expected


@given("the NIST CAVP ECC CDH vector of P-256", target_fixture="cdh_vector")
def cdh_vector():
    return P256_CAVP_CDH


@given("the reference model's 2G exists", target_fixture="double")
def reference_double():
    double = scalar_multiply(P256, 2)
    assert double is not None
    return double


@given("a low operand of the length with 1 in its last byte and a high one with 1 in its first byte", target_fixture="operands")
def compare_operands(length):
    low = bytes(length - 1) + b"\x01"
    high = b"\x01" + bytes(length - 1)
    return low, high


@when("the firmware multiplies G by the scalar", target_fixture="product")
def multiply_by_scalar(fw, expected_multiple):
    k, _ = expected_multiple
    return fw.pka.mul(k=k)


@when("the firmware multiplies G by the private key d of the vector", target_fixture="public")
def multiply_private_key(fw, cdh_vector):
    return fw.pka.mul(k=to_bytes(cdh_vector.d))


@when("the firmware multiplies the peer's public key Q of the vector by d", target_fixture="shared")
def multiply_peer_key(fw, cdh_vector):
    return fw.pka.mul(k=to_bytes(cdh_vector.d), x=to_bytes(cdh_vector.peer_x), y=to_bytes(cdh_vector.peer_y))


@when("the firmware multiplies 2G by 3 in 1 byte", target_fixture="product")
def multiply_double(fw, double):
    return fw.pka.mul(k=b"\x03", x=to_bytes(double[0]), y=to_bytes(double[1]))


@when("the firmware multiplies G by n - 2", target_fixture="product")
def multiply_by_order_minus_two(fw):
    return fw.pka.mul(k=to_bytes(P256.n - 2))


@then("the product of a multiplication without operands is G")
def default_is_g(fw):
    assert as_point(fw.pka.mul()) == P256.g


@then("the reference model multiplies G by the scalar to the expected point")
def reference_multiple(expected_multiple):
    k, expected = expected_multiple
    assert scalar_multiply(P256, from_bytes(k)) == expected, "the vector itself"


@then("both coordinates of the product have 32 bytes")
def coordinates_have_32_bytes(product):
    assert len(product.x) == len(product.y) == 32


@then("the product is the expected point")
def product_is_expected(product, expected_multiple):
    _, expected = expected_multiple
    assert as_point(product) == expected


@then("G multiplied by 5 in 1 byte, G multiplied by 5 in 32 bytes and the reference model's 5G are the same point")
def short_scalar_padded(fw):
    assert as_point(fw.pka.mul(k=b"\x05")) == as_point(fw.pka.mul(k=to_bytes(5))) == scalar_multiply(P256, 5)


@then("G multiplied by n - 1 is (Gx, p - Gy)")
def order_minus_one(fw):
    assert as_point(fw.pka.mul(k=to_bytes(P256.n - 1))) == (P256.gx, P256.p - P256.gy)


@then("the product is the public key of the vector")
def public_key(public, cdh_vector):
    assert as_point(public) == (cdh_vector.qx, cdh_vector.qy)


@then("the x coordinate of that product is the shared secret of the vector")
def shared_secret(shared, cdh_vector):
    assert from_bytes(shared.x) == cdh_vector.z


@then("that product is the reference model's d * Q")
def shared_point(shared, cdh_vector):
    assert as_point(shared) == scalar_multiply(P256, cdh_vector.d, (cdh_vector.peer_x, cdh_vector.peer_y))


@then("the product is the reference model's 6G")
def product_is_six_g(product):
    assert as_point(product) == scalar_multiply(P256, 6)


@then("the point check accepts G")
def check_accepts_g(fw):
    assert fw.pka.check(to_bytes(P256.gx), to_bytes(P256.gy))


@then("the point check accepts 2G")
def check_accepts_double(fw, double):
    assert fw.pka.check(to_bytes(double[0]), to_bytes(double[1]))


@then("the point check rejects G with y + 1")
def check_rejects_off_curve(fw):
    assert not fw.pka.check(to_bytes(P256.gx), to_bytes(P256.gy + 1))


@then("the point check rejects the point (1, 2) in 1-byte operands")
def check_rejects_short_point(fw):
    assert not fw.pka.check(b"\x01", b"\x02")


@then(parsers.parse('the comparison of low with high is "{result}"'))
def low_with_high(fw, operands, result):
    low, high = operands
    assert fw.pka.cmp(low, high) == result


@then(parsers.parse('the comparison of high with low is "{result}"'))
def high_with_low(fw, operands, result):
    low, high = operands
    assert fw.pka.cmp(high, low) == result


@then(parsers.parse('the comparison of high with high is "{result}"'))
def high_with_high(fw, operands, result):
    _, high = operands
    assert fw.pka.cmp(high, high) == result


@then(parsers.parse('the comparison of low with low with its last byte 2 is "{result}"'))
def low_with_larger_low(fw, operands, result):
    low, _ = operands
    assert fw.pka.cmp(low, low[:-1] + b"\x02") == result


@then("the firmware timed the multiplication above 0 and at most the maximum duration of the board file")
def duration(product, pka_cfg):
    assert 0 < product.us <= pka_cfg["max_us"], product.us


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
