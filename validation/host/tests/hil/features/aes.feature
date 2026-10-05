Feature: AES-128 ECB
  AES-128 ECB (`hal::SynchronousAes128EcbStm`) through the `aes` group: the FIPS-197 C.1 and SP 800-38A F.1.1 known
  answers, decryption, one to five blocks per command, key changes between commands, and every data swapping mode
  (`swap=none|half|byte|bit`, AES_CR.DATATYPE) against the model of `crypto_ref.aes_stm`. No wiring. The vectors, the
  lengths and the swapping modes are in tests.aes of the board file.

  Scenario: The firmware gives the known answers
    Given the key, plaintext and ciphertext of the vector
    Then the reference model encrypts the plaintext to the ciphertext
    And the firmware encrypts the plaintext to the ciphertext
    And the firmware encrypts the plaintext to the ciphertext with swap "byte", the default
    And the firmware decrypts the ciphertext to the plaintext

  Scenario: Each block on its own gives the same as all blocks at once
    ECB: each block on its own gives the same as all blocks in one command.
    Given the key, plaintext and ciphertext of the vector
    Then every 16-byte block of the plaintext, encrypted on its own, gives its block of the ciphertext

  Scenario: Encrypted data decrypts to itself
    Given the key is 16 PRBS bytes with seed 7
    And the data is the length of PRBS bytes seeded with the length
    When the firmware encrypts the data
    Then the ciphertext is the reference model's encryption of the data
    And the firmware decrypts the ciphertext to the data

  Scenario: The key of one command does not leak into the next
    Given the data is 32 incrementing bytes from 0
    And the first key is 16 PRBS bytes with seed 1
    And the second key is 16 PRBS bytes with seed 2
    When the firmware encrypts the data with the first key and then with the second key
    Then each ciphertext is the reference model's encryption of the data with its key
    And the two ciphertexts differ
    And the firmware encrypts the data with the first key again to the first ciphertext
    And the firmware decrypts the second ciphertext with the second key to the data

  Scenario: Encryption and decryption swap the data as AES_CR.DATATYPE says
    Encryption and decryption with the data swapping of AES_CR.DATATYPE (model: `crypto_ref.aes_stm`).
    Given the key is 16 PRBS bytes with seed 3
    And the data is 48 PRBS bytes with seed 4
    When the firmware encrypts the data with the swap
    Then the ciphertext is the swapping model's encryption of the data with the swap
    And the firmware decrypts the ciphertext with the swap to the data
    And the firmware decrypts the data with the swap to the swapping model's decryption of the data

  Scenario: Malformed commands are refused
    Then the command line fails with the reason
