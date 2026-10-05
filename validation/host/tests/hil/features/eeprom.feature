@requires_option:i2c
Feature: EEPROM
  EMIL's `eeprom.*` group over the `I2cEepromStm` adapter (`hal::Eeprom` on `hal::I2cStm`), against the external
  24Cxx of `--with i2c` (`tests.eeprom`: a 24LC256 at 0x50 by default) and, for the 1-byte word address, against the
  `i2cs` register target on the other instance.

  The adapter splits writes at page boundaries and polls the chip's address after each page (B.1 address-NACK path)
  until it acknowledges or `wcycle` ms pass; reads set the word address and read after a repeated START. An error
  outside polling prints `EVT eeprom error=... address=...` and leaves EMIL's command pending (`ERR timeout` after
  5 s); `eeprom.detach` recovers. Data written by one test is overwritten by the next, each at its own address
  range. The instance, its pins, the chip address, size, page, address bytes, bus frequency, write cycle and erase
  size are in tests.eeprom of the board file; the i2cs target is tests.i2c.loop.

  Scenario: The chip acknowledges its address
    A zero-length write (address probe) to the chip is acknowledged.
    When the I2C instance of the EEPROM is opened on its pins
    Then a zero-length write to the chip address completes

  Scenario: The attached EEPROM holds its bus until it is detached
    Given the EEPROM is attached
    Then attaching the EEPROM again on its pins fails with "busy"
    And opening the I2C instance of the EEPROM on its pins fails with "busy"
    When the EEPROM is detached
    Then detaching the EEPROM again fails with "notopen"
    And reading 1 byte at address 0 fails with "range"
    When the EEPROM is erased
    And the EEPROM is attached

  Scenario: Data written inside a page reads back
    Given the EEPROM is attached
    And the address 3 bytes into page 2
    And half a page of PRBS data with seed 11
    When the data is written there
    Then reading the data back at the address gives the data

  Scenario: A write across a page boundary does not wrap inside the page
    The adapter splits the write at the page boundary, so nothing wraps inside a page.
    Given the EEPROM is attached
    And the address 5 bytes before page 4
    And a page and 10 bytes of PRBS data with seed 12, or as much as a command line can write
    When the data is written there
    Then reading the data back at the address gives the data
    And the first 5 bytes of page 4 read as bytes 5 to 9 of the data

  Scenario: A read of a full buffer spans pages
    Given the EEPROM is attached
    And the start of page 6
    And a buffer of data incrementing from 0x30
    When the data is written there in two halves
    Then reading the data back at the address gives the data
    And reading from 1 byte past the address gives the rest of the data

  Scenario: A write after a read polls for the write cycle
    B.1h: ACK polling after a write that follows a read (stale counters used to abort or report 4294967295).
    Given the EEPROM is attached
    And the start of page 10
    Then 16 bytes of PRBS data with seed 1, then with seed 2, each written there read back

  @slow
  Scenario: Erase writes 0xFF over the erase size
    `eeprom.erase` writes 0xFF over `erase_size` (attached with that size: EMIL's 5 s limit covers it).
    Given the EEPROM is attached with the erase size of the board file as its size
    When 8 zero bytes are written at the end of the erase size
    And the EEPROM is erased
    Then the whole erase size reads 0xFF

  @resets_board
  Scenario: The data survives a reset
    Given the EEPROM is attached
    And the start of page 12
    And 8 bytes of PRBS data with seed 99
    When the data is written there
    And the board resets and the firmware forgets what was open
    And the EEPROM is attached
    Then reading the data back at the address gives the data

  Scenario: Transfers past the size are refused
    Given the EEPROM is attached
    Then writing 2 zero bytes at the last address fails with "range"
    And reading 1 byte at the size fails with "range"
    And reading 2 bytes at the last address fails with "range"
    When 0xA5 is written at the last address
    Then the last address reads 0xA5

  @ad3
  Scenario: The logic analyser sees the page write and the first NACKed poll
    At 100 kHz from the START of the page write: the control byte (0xA0 for a chip at 0x50), the word address and
    the data, then the first poll (the address bytes again) NACKed during the write cycle.
    Given the logic analyser DIOs on the SCL and SDA pins of the EEPROM
    And the EEPROM is attached at 100000 Hz
    And the start of page 14
    And 4 bytes of PRBS data with seed 3
    When the logic analyser is armed on a START for 2 ms at up to 2 MHz, with 1 % pretrigger
    And the data is written there
    Then the logic analyser decodes the page write and at least one more transfer within 2 s
    And the first transfer writes the word address and the data to the chip, acknowledged and ended by a STOP
    And the second transfer, the first poll, addresses the chip and is not acknowledged

  @slow
  Scenario: Detach recovers from an error outside polling
    No device at 0x57: the first page write is NACKed (`EVT eeprom error=nack address=0`), EMIL answers
    `ERR timeout` and stays busy; `eeprom.detach` completes the operation and a new attach works.
    Given the EEPROM is attached at address 0x57
    When the pending EEPROM errors are cleared
    Then writing the byte 0x01 at address 0 fails with "timeout"
    And the EEPROM reports exactly one error, nack at address 0
    And reading 1 byte at address 0 fails with "busy"
    When the EEPROM is detached
    And the EEPROM is attached
    Then writing the byte 0x01 at the start of page 16 succeeds

  Scenario: Detach during a transfer is refused
    `eeprom.detach` while the adapter is in a transfer answers `ERR busy` (the longest write the command line
    holds spans two pages, two write cycles).
    Given the firmware is not the fake, which completes every write at once
    And the EEPROM is attached
    And the start of page 18
    When the longest write a command line holds is begun there, with EMIL's timeout on top of the command timeout
    And the EEPROM is detached without waiting for a reply
    Then the detach is answered first, with "busy"
    And the first 8 bytes there read as the default pattern

  Scenario: One-byte word addresses reach the registers of the i2cs target
    `abytes=1` and 16-byte pages against the `i2cs` register file at 0x42 on the other instance: the word address
    is the register pointer, so the data lands in the target's registers.
    Given the i2cs target on the I2C instance of the loop that is not the EEPROM's
    And 40 bytes of PRBS data with seed 21
    And the word address 10
    When the i2cs target opens at address 0x42
    And the EEPROM is attached at address 0x42 with 256 bytes, 16-byte pages, 1 address byte and a write cycle of 0 ms
    And the data is written there
    Then reading the data back at the address gives the data
    And the registers of the i2cs target from the address hold the data
