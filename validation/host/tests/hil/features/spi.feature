Feature: SPI master
  `hal::SpiMasterStm`, `SpiMasterStmDma`, `SynchronousSpiMasterStm`, decoded from a logic-analyzer capture.

  Wiring set `bundle1`: CLK, CS, MOSI and MISO of `tests.spi.instances` on DIOs (an instance with `option` is
  reached only through that option's jumpers, e.g. WB55 SPI2 with `--with spiloop`). The firmware master is
  observed with the logic analyzer: MISO is driven to a static level by the AD3, or, where the loopback jumper ties
  the instance's MOSI to its MISO (`--with loopback`), only monitored (the firmware must then read back what it
  sent). The clock is the fastest spiclk / 2^n not above `baud` (`expect.spi_clock`). The instances, the transfer,
  session, receive-only and largest-transfer matrices, the MISO levels, the payloads, the open bauds, the session
  baud, the max transfer, the driver variants and the baud tolerance are in tests.spi of the board file.

  @ad3 @uses_option:loopback @uses_option:spiloop
  Scenario: Every payload is clocked out on MOSI and MISO is read back at the mode and clock
    Given the clock, chip select, MOSI and MISO of the instance are wired to DIOs
    And the AD3 drives MISO to the MISO level unless the loopback jumper ties it to MOSI, which a high MISO level skips
    When the instance is opened with the variant, the baud and the mode, with its chip select if the chip select is gpio
    Then every payload of the board file comes back as the payload with the loopback jumper and at the MISO level otherwise and, where the logic analyzer samples 4 times per clock, decodes on MOSI and MISO with the clock idle at CPOL, at the expected clock and the chip select released

  @ad3 @uses_option:spiloop
  Scenario: A continued transfer keeps the chip select low for the next one
    `continue=1` keeps the chip select low for the next `spi.xfer`; the bytes of both arrive in one session.
    Given the clock, chip select, MOSI and MISO of the instance are wired to DIOs
    And the AD3 drives MISO low
    When the instance is opened with the variant at the session baud and the mode
    And the logic analyzer is armed for 0.2 s on the falling chip select, unless it cannot sample 4 times per clock at the session baud
    And the instance transfers 12 34, continuing the session
    And the instance transfers 56, ending the session
    And the capture completes within 3 s
    Then the chip select rises once after the trigger
    And MOSI decodes as 12 34 56 at the mode

  @ad3 @uses_option:loopback @uses_option:spiloop
  Scenario: A receive-only transfer right after the open clocks out zeros
    A receive-only transfer (`spi.xfer <i> - rx=<n>`) right after `spi.open` clocks out zeros and returns MISO.
    Given the clock, chip select, MOSI and MISO of the instance are wired to DIOs
    And the AD3 drives MISO high unless the loopback jumper ties it to MOSI
    When the instance is opened with the variant at 1000000 baud
    Then a receive-only transfer of 4 bytes returns 4 zero bytes with the loopback jumper and 4 0xff bytes otherwise

  @ad3 @uses_option:loopback @uses_option:spiloop
  Scenario: The largest transfer and the receive lengths around it come back
    Given the clock, chip select, MOSI and MISO of the instance are wired to DIOs
    And the AD3 drives MISO high unless the loopback jumper ties it to MOSI
    And the largest payload: the max transfer of bytes counting up by 11 from 5
    When the instance is opened with the variant at 1000000 baud
    Then a transfer of the payload returns it with the loopback jumper and 0xff bytes otherwise
    And a transfer of the first 8 bytes of the payload receiving none returns nothing
    And a transfer of the first 2 bytes of the payload receiving 6 returns those and 4 zero bytes with the loopback jumper and 6 0xff bytes otherwise
    And a receive-only transfer of 4 bytes returns 4 zero bytes with the loopback jumper and 4 0xff bytes otherwise

  Scenario: A baud outside the SPI clock limits is refused
    `baud` outside spiclk/256 .. spiclk/2 is `ERR range`.
    When the instance is opened at the baud, which may be refused
    Then it was refused with "range" exactly when the baud is outside spiclk/256 .. spiclk/2
    And an accepted open closes again

  Scenario: Invalid opens are refused
    Then opening the instance without MISO, with both dma and sync, with mode 4, at baud 0, with clock and MISO swapped, with the terminal pin or with its clock as chip select fails with usage, usage, range, range, pin, busy and busy
    And the instance opens synchronously with its pins and its chip select

  Scenario: Transfers on a closed instance and malformed transfers are refused
    Then a transfer of 00 on the instance fails with "notopen"
    When the instance is opened with its pins
    Then transfers with no data, a malformed receive length, odd hex, one byte over the max transfer, a receive length over it or continue=2 fail with usage, usage, usage, range, range and range
