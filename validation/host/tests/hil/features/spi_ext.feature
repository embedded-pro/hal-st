@uses_option:loopback @uses_option:spiloop
Feature: SPI master extensions
  PROTOCOL.md "SPI master": the bit order (`lsb=1`), frame sizes (`bits=`, a `hal::SpiDataSizeConfiguratorStm` on
  `hal::SpiMasterStmDma`), an 8-bit master reopened after a 16-bit one and the hardware slave select (`nss=`).

  The master is observed with the logic analyzer (`tests.spi_ext.instances`; an entry with `option` is reached only
  through that option's jumpers). Words are decoded with `spiwords` at the frame size and compared with the driver's
  frame model (`spiwords.spi_frames`: frames above 8 bits take two buffer bytes). MISO is a static AD3 level, or
  follows MOSI where the loopback jumper ties them (`--with loopback`). `nss=` is a known gap (DESIGN B.14): the
  masters initialise SPI_NSS_SOFT, so NSS is never driven and `test_hardware_nss` is an expected failure. The
  instances, the bit orders, the baud and the payload are in tests.spi_ext of the board file, the driver variants in
  tests.spi.variants.

  @ad3
  Scenario: The bit order holds on every driver
    `lsb=1` sends and receives the least significant bit first (`Config::msbFirst`), on every driver.
    Given the clock, MOSI, MISO and chip select of the instance are wired to DIOs, MISO driven high unless the loopback jumper ties it to MOSI
    When the master is opened with the variant and the bit order
    And the payload is transferred while the logic analyzer records from the falling chip select, unless it cannot sample 4 times per clock
    Then MOSI decodes, in the bit order, as the payload and MISO as the payload with the loopback jumper and 0xff bytes otherwise
    And the master received the payload with the loopback jumper and 0xff bytes otherwise

  @ad3
  Scenario: Every listed frame size clocks frames of that size
    `bits=<n>` clocks n-bit frames (`spiwords.spi_frames`); the received frames come back right aligned.
    Given the clock, MOSI, MISO and chip select of the instance are wired to DIOs, MISO driven high unless the loopback jumper ties it to MOSI
    When the master is opened with the frame size
    And the payload is transferred while the logic analyzer records from the falling chip select, unless it cannot sample 4 times per clock
    Then MOSI decodes, at the frame size, as the frames of the payload and MISO as those frames with the loopback jumper and all ones otherwise
    And the master received the MISO frames as buffer bytes

  @ad3
  Scenario: A master reopened at 8 bits after a 16-bit open runs 8-bit frames
    Every open builds new DMA streams, whose constructor resets the data widths, so this cannot catch a width that
    sticks on a live GPDMA channel (B.4a is proved by review; its 32-bit encoding by
    test_dma.py::test_wave_32_bit).
    Given the instance has 16-bit frames
    And the clock, MOSI, MISO and chip select of the instance are wired to DIOs, MISO driven high unless the loopback jumper ties it to MOSI
    When the master is opened with 16-bit frames
    And the master transfers 34 12 78 56
    And the master is closed
    And the master is opened
    And the payload is transferred while the logic analyzer records from the falling chip select, unless it cannot sample 4 times per clock
    Then MOSI decodes as the payload
    And the master received the payload with the loopback jumper and 0xff bytes otherwise

  @ad3
  Scenario: The hardware slave select is low while the master clocks
    `nss=<pin>`: NSS low while the master clocks, high after the transfer. Known gap (B.14): the masters initialise
    SPI_NSS_SOFT, so the pin (pulled up by the AD3 here) never goes low.
    Given the clock, MOSI, MISO and NSS of the instance are wired to DIOs, MISO driven high unless the loopback jumper ties it to MOSI
    And the AD3 pulls NSS up
    When the master is opened with the variant and the hardware slave select
    And the payload is transferred while the logic analyzer records from the first clock edge, unless it cannot sample 4 times per clock
    Then the clock was captured
    And NSS is low at every clock edge
    And NSS is high at the end of the capture

  Scenario: Frame sizes the driver or the instance cannot run are refused
    `bits` 4..16 needs `dma=1` (8 is the default and needs nothing); a limited instance (WBA55 SPI3,
    `IS_SPI_LIMITED_INSTANCE`) takes 8 and 16 only; anything else is `ERR unsupported`, outside 4..16 `ERR range`.
    Then every frame size from the smallest to the largest opens with dma and closes again, unless the instance is limited and the size is not 8 or 16, which fails with "unsupported"
    And opening with dma one size below the smallest or above the largest, with 16 bits without dma, synchronously or with lsb=2 fails with range, range, unsupported, unsupported and range
    And the instance opens at the default frame size least significant bit first and closes again

  Scenario: The hardware slave select must be the instance's and excludes the chip select
    `nss` must offer the instance's slave select (`ERR pin`) and excludes the GPIO chip select `cs` (`ERR usage`).
    Then opening with both the slave select and the chip select or with the clock as slave select fails with "usage" and "pin"
    When the instance is opened with dma and its slave select
    Then configuring the slave select pin as a GPIO output fails with "busy"
