@uses_option:spiloop
Feature: SPI slave
  `hal::SpiSlaveStmDma`, PROTOCOL.md "SPI slave", clocked by the AD3 SPI master and, with `--with spiloop`, by the
  board's own SPI master on the other instance.

  The AD3 master runs mode 0, MSB first, with its chip select on the slave's NSS (`tests.spis.instances`; an entry
  with `option` is reached only through that option's jumpers). Slave and master send different payloads, so each
  side checks what the other received; results above 128 bytes are compared by CRC. Every case may run with the
  spiloop jumpers fitted: they tie the pins to the other SPI instance, which stays unconfigured. The instances, the
  frequencies, the lengths, the loops with their variants, lengths and baud are in tests.spis of the board file, the
  driver variants in tests.spi.variants.

  @ad3
  Scenario: The slave transfers in full duplex, send only and receive only
    Full duplex, send only (`rx=0`) and receive only (`-` with `rx=<n>`); the slave payload is generated in
    firmware (`len=`, `pattern=prbs`).
    Given the AD3 SPI master is configured at the frequency on the pins of the slave
    And the slave is opened
    And the slave sends the length of PRBS bytes seeded with the length
    And the AD3 master sends the length of bytes counting up from 0x40
    When the slave is armed for the direction, generating its payload in firmware unless it only receives
    And the AD3 master transfers its payload
    Then the AD3 master received the payload of the slave, unless the slave only receives
    And the slave received the payload of the AD3 master, nothing if the slave only sends

  @ad3
  Scenario: Frames clocked while nothing is armed do not lead the next transfer
    B.2: SPE off between transfers, the STM32WB RX FIFO drained before arming.
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    Then the slave sending "10111213" and the AD3 master sending "a0a1a2a3" exchange them
    When the AD3 master clocks "deadbeef"
    Then the slave sending "20212223" and the AD3 master sending "b0b1b2b3" exchange them

  @ad3
  Scenario: A master clocking more than armed completes the armed transfer
    Armed for 4 bytes, clocked for 8: done with the first 4 (B.2); the next transfer is exact.
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    When the slave is armed with "31323334"
    Then the AD3 master clocking 8 bytes counting up from 0xc0 receives the armed bytes first
    And the slave received the first 4 bytes of the AD3 master
    And the slave sending "41424344" and the AD3 master sending "d0d1d2d3" exchange them

  @ad3
  Scenario: Clocks with nothing armed complete nothing
    Clocks with no transfer armed complete nothing (`done=0` at once); an armed transfer afterwards is exact.
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    When the AD3 master clocks "01020304"
    Then the slave reports no completed transfer
    And the slave sending "51525354" and the AD3 master sending "e0e1e2e3" exchange them

  @ad3
  Scenario: A short transfer stays pending until it is cancelled
    Armed for 8 bytes, clocked for 4: `done=0` after the wait; `spis.cancel` stops it (`cancelled=1`) and the
    next transfer is exact.
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    When the slave is armed with 8 bytes counting up from 0x60
    Then the AD3 master clocking "71727374" receives the first 4 armed bytes
    And the slave reports no completed transfer after waiting 100 ms
    And cancelling the slave transfer reports a cancelled transfer
    And the slave reports no completed transfer
    And cancelling it again reports nothing to cancel
    And the slave sending "81828384" and the AD3 master sending "f0f1f2f3" exchange them

  @ad3
  Scenario: A waiting result answers when the transfer completes
    `spis.result` armed and not done answers when the transfer completes.
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    When the slave is armed with "9192"
    And the AD3 master clocks "9394" and receives "9192" while a result waiting up to 5000 ms is pending
    Then the pending result answers that the transfer is done, having received "9394"

  @ad3
  Scenario: A second result while one waits is busy
    A `spis.result` while another waits answers `ERR busy` at once; the waiting one still answers `done=1`.
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    When the slave is armed with "a1a2"
    And a result waiting up to 5000 ms is requested
    And a second result is sent without waiting for its answer
    And the first answer is settled as the answer to the pending result
    And the terminal history is cleared
    And the AD3 master clocks "a3a4"
    And the terminal goes quiet for 0.3 s
    Then that answer is "ERR busy"
    And the terminal history holds "OK done=1 rx=a3a4"

  @ad3
  Scenario: A rejected arm keeps the armed payload
    An `spis.arm` while a transfer is armed answers `ERR busy` before it parses its payload: the transmit DMA
    still reads the armed payload, longer than any SPI FIFO here, and the master receives it unchanged.
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    When the slave is armed with 32 PRBS bytes seeded with 11
    Then a second arm with a hex payload, with a payload and a receive length and with a generated payload each answers "ERR busy" before it parses the payload
    And the AD3 master clocking 32 bytes counting up from 0x30 receives the armed payload
    And the slave received the bytes of the AD3 master

  @ad3
  Scenario: The slave reopens after a close
    Given the AD3 SPI master is configured at the first frequency on the pins of the slave
    And the slave is opened
    Then the slave sending "b1b2b3b4" and the AD3 master sending "0b1b2b3b" exchange them
    When the slave is closed
    And the slave is opened
    Then the slave sending "c1c2c3c4" and the AD3 master sending "0c1c2c3c" exchange them

  @requires_option:spiloop
  Scenario: The board's own master transfers with the slave on the other instance
    The board's master (`spi.*`, the variant's driver) against the slave on the other instance; the master's
    GPIO chip select drives the slave's NSS through the jumpers.
    Given the slave of the loop is opened
    And the master of the loop is opened with its chip select at the loop baud with the driver of the variant
    When the slave of the loop is armed with the length of PRBS bytes seeded with 7
    Then the master of the loop transferring the length of bytes counting up from 0x80 receives the armed payload
    And the slave of the loop reports that it received the bytes of the master

  Scenario: Invalid opens are refused
    Then opening the slave without its slave select, with clock and MISO swapped or with the clock as slave select fails with usage, pin and pin
    And opening the slave with a chip select or with an extra argument fails with "usage"
    When the slave is opened
    Then opening the slave again fails with "busy"
    And opening the SPI master on the instance fails with "busy"
    When the slave is closed
    Then the SPI master opens on the instance and closes again

  Scenario: Invalid arms and results are refused
    Then arming the slave with 01 fails with "notopen"
    When the slave is opened
    Then every malformed or out-of-range spis.arm, spis.result and spis.cancel fails with its reason
    And with nothing armed the slave reports no completed transfer at once
    When the slave is armed to receive one byte more than the hex output holds
    Then a result without waiting fails with "range"
    And a CRC result waiting 0 ms reports no completed transfer
    And arming the slave with 01 fails with "busy"
    And arming the slave with 0102 and a receive length fails with "busy"
    And a CRC result waiting 50 ms reports no completed transfer
    And cancelling the slave transfer reports a cancelled transfer
    And cancelling it again reports nothing to cancel
    When the slave is armed to send 01 02 only
    And the slave is closed
