@family:stm32wb55
Feature: QUADSPI
  QUADSPI (`hal::QuadSpiStm`, `hal::QuadSpiStmDma`, `hal::SingleSpeedQuadSpiStmDma`) through the `qspi` group,
  NUCLEO-WB55RG only.

  Wiring set `bundle1` (or `bundle2`): every QUADSPI pin is on a DIO (CLK PA3 DIO13, NCS PA2 DIO12, IO0 PB9 DIO5,
  IO1 PB8 DIO3, IO2 PA7 DIO1, IO3 PA6 DIO7). The AD3 never drives a line the QUADSPI drives: writes are decoded with
  the logic analyzer only; reads take their data from AD3 levels on lines the QUADSPI only receives on (IO1 in
  single-line mode; IO0-IO3 in a 4-line read without instruction, address or alternate bytes, driven only between
  `qspi.open` and the reply, with the weakest AD3 drive). Every case resolves the six pins through the load gate, so
  `--with loopback` (PA6-PA7), `--with spiloop` (PA6, PA7) and `--with i2c` (PB8, PB9) skip them.

  A 4-line read with a data phase only hangs the QUADSPI with BUSY set (the STM32 QUADSPI erratum "cannot be used in
  indirect read mode when only data phase is activated"; its workaround is two dummy cycles), so the receive tests
  add `dummy=2`, which keeps IO0-IO3 undriven. `SingleSpeedQuadSpiStmDma` receives with a data phase only
  (`SingleSpeedQuadSpiStmDma.cpp:27`): test_xfer_receive shows whether the erratum hits it. The tests that can leave
  the QUADSPI busy reset the board when they fail, so the next test starts clean.

  B.15 (QuadSpiStmDma completes a write on the DMA transfer-complete, while the FIFO still drains): writes answer
  `flevel`, the FIFO level sampled in the completion callback, which must be 0 (test_write_completes_after_last_byte),
  and two writes issued from one completion callback must both reach the bus (test_back_to_back_writes).

  The instance, the pins, the prescalers, the write, the instructions, the read length, the nibbles, the poll, the
  back-to-back writes and the transfer bytes are in tests.qspi of the board file.

  Scenario: The open reports the QUADSPI clock
    Then opening the instance with the variant and the prescaler reports the QUADSPI kernel clock over the prescaler plus one

  Scenario: Malformed and out-of-range opens are refused
    Then every malformed or out-of-range open answers its reason
    When the instance opens with variant dma and the largest size
    Then opening the instance fails with "busy"

  Scenario: The open holds the six pins
    `qspi.open` claims the six pins of the board profile: a pin held elsewhere makes it busy, and its pins are busy
    for the other groups until `qspi.close`.
    Given IO2 is configured as a GPIO output
    Then opening the instance fails with "busy"
    When IO2 is released
    And the instance opens with variant spi
    Then configuring any of the six pins as a GPIO input fails with "busy"
    When the instance closes
    Then CLK can be configured as a GPIO input

  Scenario: The commands need an open instance
    Then every command on the closed instance answers "notopen"
    And every command on instance 2 answers "range"

  Scenario: Malformed and out-of-range commands are refused
    When the instance opens with variant dma
    Then every malformed or out-of-range command answers its reason

  Scenario: Malformed and out-of-range polls are refused
    When the instance opens with variant dma
    Then every malformed or out-of-range poll answers its reason

  Scenario: Transfers need variant spi and one direction
    `qspi.xfer` needs `variant=spi` and exactly one of tx and rx: `SingleSpeedQuadSpiStmDma` is half duplex.
    When the instance opens with variant dma
    Then a transfer of 9f fails with "unsupported"
    When the instance closes
    And the instance opens with variant spi
    Then every malformed or out-of-range transfer answers its reason

  Scenario: Every phase is optional and every command answers its reply
    Writes answer `flevel=0`, reads their data (or `len` and `crc` with `out=crc`); every phase is optional. The
    reads have an instruction phase (read_instr), so they never take the data-only path of the erratum.
    When the instance opens with the variant
    Then a write of the instruction alone answers flevel 0
    And the write of the board file on 4 lines answers flevel 0
    And a full-buffer 1-line PRBS write with seed 7 answers flevel 0
    And a 1-line read with the read instruction answers the read length of data
    And a full-buffer 4-line read with the read instruction, address 0 and two dummy cycles answers an 8-digit CRC
    And with variant spi, a transfer of the transfer bytes answers flevel 0

  Scenario: A write completes after its last byte left the FIFO
    B.15: at the slowest clock the last bytes of a write take a while to leave the FIFO; the completion
    callback must see it empty.
    When the instance opens with the variant at the slow prescaler
    And a 1-line PRBS write with seed 1 of the FIFO-level length is made, as a transfer with variant spi and after the instruction otherwise
    Then the write answers flevel 0

  @ad3
  Scenario: A write carries every phase on the bus
    Instruction, address, alternate bytes and data of a write on the bus, all on 1 or on 4 lines.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with the variant at the decode prescaler
    And the logic analyzer is armed on NCS falling for the write of the board file on the lines
    Then the write of the board file on the lines answers flevel 0
    And the first chip-select frame captured carries the instruction, address, alternate bytes and data of the write on the lines

  @ad3 @resets_board
  Scenario: Back-to-back writes both reach the bus
    B.15: `repeat=2` issues the second write from the completion callback of the first; both reach the bus.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with the variant at the decode prescaler
    And the logic analyzer is armed on NCS falling for the back-to-back 1-line PRBS writes, after the instruction unless the variant is spi
    And any failure from here on resets the board
    And the back-to-back writes are issued, as transfers with variant spi and as commands otherwise
    Then the writes answer flevel 0
    And every chip-select frame captured carries the instruction and the payload, once per repeat

  @ad3 @resets_board
  Scenario: A 4-line read receives the nibble the AD3 holds
    The first command after `qspi.open` is a 4-line read without instruction, address or alternate bytes, so the
    QUADSPI drives none of IO0-IO3 while the AD3 holds them at `nibble`: every byte is the nibble twice.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with the variant at the decode prescaler
    And any failure from here on resets the board
    And a 4-line read of the read length with two dummy cycles runs while the AD3 holds IO0-IO3 at the nibble with its weakest drive, releasing them afterwards
    Then every byte read is the nibble twice

  @ad3
  Scenario: A status poll matches at once
    A 1-line instruction followed by a 1-line status read on IO1, which the AD3 holds high: the status matches
    at once. The status is read once first, because one that never matches blocks `variant=poll` for good.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with the variant at the decode prescaler
    And the AD3 holds IO1 at 1
    Then a 1-line read of one status byte with the poll instruction reads ff
    When the logic analyzer is armed on NCS falling for 2 bytes on 1 line
    And the instance polls for the status match with the poll instruction on 1 line
    Then the first chip-select frame captured carries the poll instruction on IO0 and the status ff on IO1

  @ad3
  Scenario: A full-buffer read answers the CRC-32 of the level the AD3 holds
    A full-buffer 1-line read of IO1, which the AD3 holds at `level`, answers the CRC-32 of those bytes.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with the variant
    And the AD3 holds IO1 at the level
    Then a full-buffer 1-line read with the read instruction answers the CRC-32 of bytes of the level

  @ad3 @slow @resets_board
  Scenario: A status that never matches times out and the close stops the polling
    A status that never matches answers `ERR timeout` after 2 s; the group stays busy until `qspi.close`, which
    stops the polling (`~QuadSpiStmDma` clears CR), and a new open works. `variant=poll` blocks instead (B.14):
    the AD3 then makes the status match, so the firmware returns.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with the variant
    And the AD3 holds IO1 at 0
    And any failure from here on resets the board
    And a 1-line status poll with the poll instruction runs, the AD3 driving IO1 high and the line going quiet if no answer comes
    Then the poll answers "timeout"
    And a write of the instruction alone fails with "busy"
    When the instance closes
    And the AD3 releases IO1
    And the instance opens with the variant
    Then a write of the instruction alone answers flevel 0

  @slow @resets_board
  Scenario: The close recovers from a data-only read
    A 4-line read with a data phase only hangs the QUADSPI with BUSY set (erratum), which only an abort or a
    reset clears: `variant=dma` answers `ERR timeout`. `qspi.close` resets the QUADSPI, so after a new open both
    drivers complete a write again (`QuadSpiStm` would otherwise wait in `HAL_QSPI_Init` and fail every command).
    When the instance opens with variant dma
    And any failure from here on resets the board
    And a 4-line read of 4 bytes with a data phase only runs
    Then the read answers "timeout", unless it completed and the scenario skips
    When the instance closes
    Then with variant dma and then poll, a new open completes a write of the instruction alone and closes

  @ad3
  Scenario: A transfer is SPI mode 0 on IO0
    `variant=spi`: `SingleSpeedQuadSpiStmDma` writes are SPI mode 0 with MOSI on IO0 and NCS as chip select.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with variant spi at the decode prescaler
    And the logic analyzer is armed on NCS falling for the transfer bytes on 1 line
    Then a transfer of the transfer bytes answers flevel 0
    And the capture decodes as SPI mode 0 with MOSI on IO0 and NCS as chip select to the transfer bytes, the clock idling low

  @ad3 @resets_board
  Scenario: A transfer receives the level the AD3 holds on IO1
    `variant=spi` receives on IO1 (MISO), which the AD3 holds at `level`; the read has a data phase only.
    Given the six QUADSPI pins are on DIOs
    When the instance opens with variant spi at the decode prescaler
    And the AD3 holds IO1 at the level
    And any failure from here on resets the board
    Then a transfer receiving the read length reads bytes of the level
