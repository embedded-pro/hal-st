@family:stm32wb55 @family:stm32wba55
Feature: Public key accelerator
  The public key accelerator (`hal::PkaStm` on `services::secp256r1`) through the `pka` group: scalar multiplication
  (multiples of G, the CAVP ECC CDH vector, n - 1, operands shorter than 32 bytes padded by the firmware), the point
  check on and off the curve, the comparison of 4-60 byte operands and the duration of one multiplication. Results
  are checked against `crypto_ref`'s affine P-256 arithmetic. No wiring. The multiples, the comparison lengths and the
  maximum duration are in tests.pka of the board file.

  Scenario: Without operands the firmware multiplies G by 1
    Then the product of a multiplication without operands is G

  Scenario: The firmware multiplies G by the scalar of the multiple
    Given the scalar and the expected point of the multiple
    Then the reference model multiplies G by the scalar to the expected point
    When the firmware multiplies G by the scalar
    Then both coordinates of the product have 32 bytes
    And the product is the expected point

  Scenario: A short scalar is padded
    A 1-byte scalar equals the same value in 32 bytes: the firmware left-pads every operand.
    Then G multiplied by 5 in 1 byte, G multiplied by 5 in 32 bytes and the reference model's 5G are the same point

  Scenario: G multiplied by n - 1 is the negation of G
    Then G multiplied by n - 1 is (Gx, p - Gy)

  Scenario: The firmware passes the NIST CAVP ECC CDH vector
    NIST CAVP ECC CDH (P-256): d * G is the public key, d * Q has the shared secret as x coordinate.
    Given the NIST CAVP ECC CDH vector of P-256
    When the firmware multiplies G by the private key d of the vector
    Then the product is the public key of the vector
    When the firmware multiplies the peer's public key Q of the vector by d
    Then the x coordinate of that product is the shared secret of the vector
    And that product is the reference model's d * Q

  Scenario: The given point replaces G
    `x`/`y` replace G: 3 * (2G) = 6G.
    Given the reference model's 2G exists
    When the firmware multiplies 2G by 3 in 1 byte
    Then the product is the reference model's 6G

  Scenario: The point check accepts the points on the curve and rejects the others
    Given the reference model's 2G exists
    Then the point check accepts G
    And the point check accepts 2G
    And the point check rejects G with y + 1
    And the point check rejects the point (1, 2) in 1-byte operands

  Scenario: The comparison orders operands of the length
    Given a low operand of the length with 1 in its last byte and a high one with 1 in its first byte
    Then the comparison of low with high is "lt"
    And the comparison of high with low is "gt"
    And the comparison of high with high is "eq"
    And the comparison of low with low with its last byte 2 is "lt"

  Scenario: The firmware times a multiplication
    One multiplication with a full 256-bit scalar is timed by the firmware (`us`).
    When the firmware multiplies G by n - 2
    Then the firmware timed the multiplication above 0 and at most the maximum duration of the board file

  Scenario: Malformed and out-of-range commands are refused
    Then the command line fails with the reason
