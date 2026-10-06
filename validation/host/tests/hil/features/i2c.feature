@uses_option:i2c
Feature: I2C
  I2C master (`hal::I2cStm`) through the `i2c` group, against the `i2cs` LL target scaffold on the other instance.

  Standalone cases (`tests.i2c.instances`, bundle1, no option): each instance on its own pins with the MCU pull-ups
  (`pull=up`), enough for Standard mode on a short bus: the default and computed TIMINGR, address NACK and its
  recovery, the zero-length probe, arbitration loss (the AD3 holds SDA low before `i2c.open`, or pulls it low inside
  the second address bit after a START) and the argument errors. Nobody answers on these buses, so every transfer
  ends in an address NACK.

  `--with i2c` cases (`tests.i2c.loop`): the master and the target on the jumpered rows with 4.7 kOhm pull-ups and
  the 24LC256 at 0x50: Fast mode, rise time (where a scope sits on the rows), data NACK at every position, the
  address NACK after the other direction (B.1h), RELOAD lengths, repeated START, continued sessions in both
  directions (B.1d), clock stretching, a bus error from the target's misplaced STOP (B.1e), the general call against
  an idle I2cStm (B.1i), close while the bus is held, and both instance directions.

  The feature is tagged `uses_option:i2c`: the standalone cases also run with the option fitted (they then share the
  rows with the pull-ups and the EEPROM, which answers neither 0x10 nor 0x7f). Checks of the physical bus (levels,
  timing, rise time) skip under `--fake`; the fake answers the protocol. The instances, the loop, the frequencies,
  the tolerance, the rise limits, the absent and arbitration addresses, the NACK positions and length, the payload
  lengths, the stretch times and the fault frequency are in tests.i2c of the board file.

  Scenario: Open reports the default, the computed and the given TIMINGR
    Without `freq` the driver's default `Config` (0x70B03D3D on STM32WB/WBA, B.1f); with `freq` the `I2cTiming`
    value for the reported kernel clock; `timing=` is written as given.
    When the instance is opened standalone with the MCU pull-ups
    Then it reports the default TIMINGR of the driver's Config
    When the instance is closed
    Then opened standalone at every standalone and option frequency of the board file, it reports the I2cTiming value for the reported kernel clock, and closes
    And opened standalone with the TIMINGR 0x10B0172F, it reports that TIMINGR

  @ad3
  Scenario: The default TIMINGR runs Standard mode on the bus
    B.1f: the default TIMINGR runs Standard mode on the bus: tLOW >= 4.7 us, tHIGH >= 4.0 us, f <= 100 kHz.
    Given the logic analyser DIO of the SCL pin of the instance and that of SDA if it is observed
    When the instance is opened standalone with the MCU pull-ups
    And the logic analyser is armed for 400 us of the bus at 100000 Hz
    Then a write of 0x00 from the instance to the absent address answers "nack"
    And the SCL capture, within 2 s, runs at up to 101 % of 100 kHz with the tLOW and tHIGH minima of Standard mode

  @ad3
  Scenario: SCL runs at the requested frequency with the mode's tLOW and tHIGH
    Given the logic analyser DIO of the SCL pin of the instance and that of SDA if it is observed
    When the instance is opened standalone at the frequency with the MCU pull-ups
    And the logic analyser is armed for 30 SCL periods at the frequency
    And the instance writes 0x00 to the absent address
    Then the SCL capture, within 2 s, runs within the tolerance of the frequency with the tLOW and tHIGH minima of the mode

  Scenario: An address NACK is reported and recovers
    B.1a/b: an address NACK answers `nack` (`sent=0`) after `EVT i2c hook=notfound`, and the same command behaves
    the same again: no stale NACKF, no callback left armed.
    When the instance is opened standalone with the MCU pull-ups
    Then twice in a row, a transfer in the direction to the absent address answers "nack" with no byte sent, and the instance reports only the "notfound" hook

  @ad3
  Scenario: The NACKed address is on the bus, followed by a STOP
    The address and the R/W bit on the bus, NACKed, followed by a STOP.
    Given the logic analyser DIOs of the SCL and SDA pins of the instance
    When the instance is opened standalone at 100000 Hz with the MCU pull-ups
    Then a write of 0x55 and then a read of 1 byte from the absent address, each captured for 400 us at 100000 Hz, each start with the absent address and its R/W bit, NACKed, without data and followed by a STOP

  Scenario: A write without data is an address probe
    `-` without `len` writes no data (NBYTES=0, R2): an address probe that answers `nack` here.
    When the instance is opened standalone with the MCU pull-ups
    And the instance sends a zero-length write to the absent address
    Then the reply has 0 bytes sent and the result "nack"
    And the instance reports only the "notfound" hook

  @ad3
  Scenario: SDA held low before the open loses arbitration, and the instance recovers
    B.1e: SDA held low by the AD3 before `i2c.open` (no START is latched, BUSY stays 0): the first address 1 bit
    loses arbitration (`buserror`, `EVT hook=arblost`); once SDA is released the same instance works (no device:
    `nack`).
    Given the logic analyser DIO of the SDA pin of the instance
    Then with the AD3 driving SDA low from before the open, the instance opened standalone at 100000 Hz answers "buserror" to a transfer in the direction to the arbitration address and reports the "arblost" hook, then the AD3 releases SDA
    And a write of 0x00 from the instance to the arbitration address answers "nack"
    And the instance reports only the "notfound" hook

  @ad3
  Scenario: SDA pulled low inside an address bit loses arbitration, and the instance recovers
    B.1e: an open instance writes to 0x7f while the AD3 pulls SDA low for one SCL period inside the second address
    bit (started by the START detector): `buserror` and `EVT hook=arblost`, then a good transfer.
    Given the logic analyser DIOs of the SCL and SDA pins of the instance
    When the instance is opened standalone at 100000 Hz with the MCU pull-ups
    Then with the AD3 pulling SDA low for one SCL period inside the second address bit after a START, a write of 0x00 to the arbitration address answers "buserror" and the instance reports the "arblost" hook, then the AD3 pattern stops
    And a write of 0x00 from the instance to the arbitration address answers "nack"

  Scenario: Malformed, out-of-range and conflicting commands are refused
    Given the first standalone instance, with its pins resolved, that no enabled option loads
    Then every malformed, out-of-range or not-open command line on it fails with its reason
    When it is opened with the MCU pull-ups
    Then every command line that claims its pins again or that the open instance refuses fails with its reason
    When it is closed and the i2cs target opens on its pins
    Then opening it as an I2C master fails with "busy"

  @requires_option:i2c
  Scenario: The external pull-ups win against the MCU pull-down
    The external pull-ups win against the MCU pull-down on every row pin.
    Then every SCL and SDA pin of the master and the target, configured as an input with a pull-down, reads 1

  @conflicts_option:i2c
  Scenario: Without the option nothing on the board pulls the I2C pins up
    Without the option nothing on the board pulls the I2C pins up (documents WBA55 PB1/PB2): the MCU pull-down
    wins, which is why the standalone cases need `pull=up`.
    Then each of the SCL and SDA pins of the instance, once checked to be loaded by no enabled option, configured as an input with a pull-down reads 0

  @ad3 @requires_option:i2c
  Scenario: SCL runs at the requested frequency with the external pull-ups
    Standard and Fast mode with the external pull-ups: frequency within tolerance and the tLOW/tHIGH minima.
    When the loop is opened at the frequency
    And the logic analyser DIOs of the SCL and SDA pins of the master
    And the logic analyser is armed for 60 SCL periods at the frequency
    Then writing the inc pattern of 4 bytes to the target answers "complete"
    And the SCL capture, within 2 s, runs within the tolerance of the frequency with the tLOW and tHIGH minima of the mode

  @ad3 @requires_option:i2c
  Scenario: SCL and SDA rise within the limit of the mode
    30-70 % rise time of SCL and SDA on the rows (needs a scope on the bus: WBA55 bundle2; the WB55 scopes stay on
    the ADC inputs, A.3), within `rise_ns` of the mode.
    Given the scope channels on the SCL and SDA pins of the master, which the wiring set must have
    When the loop is opened at the frequency
    And the scope is armed on SCL rising through half of VDD, 8192 samples at 100 MHz
    And the master writes 64 bytes of PRBS data with seed 1 to the target
    Then every rise of SCL and SDA from 30 % to 70 % of VDD, captured within 2 s, is within the rise time of the mode of the frequency

  @requires_option:i2c
  Scenario: The target acknowledges its address
    When the loop is opened
    Then a zero-length write to the target answers "complete"
    And the master reports no hook
    And the target counts 1 write

  @requires_option:i2c
  Scenario: A data NACK counts the acknowledged bytes
    B.1a/c: the target NACKs the byte at the position: `sent` counts the acknowledged bytes and no address NACK is
    reported.
    Given the byte to NACK is the one at the position
    When the loop is opened with the target in sink mode
    And the target is set to NACK that byte
    And the master writes the NACK length of PRBS data seeded with the position of that byte
    Then the reply counts the bytes before that byte as sent and answers "nack"
    And the master does not report the "notfound" hook
    And the target received the bytes before that byte and NACKed 1
    When the target's configuration is reset
    Then writing the NACK length to the target answers "complete"

  @requires_option:i2c
  Scenario: A data NACK of the next-to-last or the last byte
    B.1c: a NACK of the next-to-last and of the last byte (the empty `onReceived` used to abort). B.1a/c: `sent`
    counts the acknowledged bytes and no address NACK is reported.
    Given the byte to NACK is the from-end count of bytes before the last byte of the NACK length
    When the loop is opened with the target in sink mode
    And the target is set to NACK that byte
    And the master writes the NACK length of PRBS data seeded with the position of that byte
    Then the reply counts the bytes before that byte as sent and answers "nack"
    And the master does not report the "notfound" hook
    And the target received the bytes before that byte and NACKed 1
    When the target's configuration is reset
    Then writing the NACK length to the target answers "complete"

  @requires_option:i2c
  Scenario: An address NACK after a transfer in the other direction
    B.1h: a read, then a write to an absent address (`sent=0`, `EVT notfound`); a write, then a read from an absent
    address (`nack`, `EVT notfound`).
    When the loop is opened
    Then a read of 4 bytes from the target answers "complete"
    And a write of 0x01 from the master to the absent address answers "nack" with 0 bytes sent
    And the master reports only the "notfound" hook
    And a write of 0x00 0x01 to the target answers "complete"
    And a read of 4 bytes from the master at the absent address answers "nack"
    And the master reports only the "notfound" hook

  @requires_option:i2c
  Scenario: The register file reads back what was written
    Register file: the first written byte sets the pointer; a write and a restart read return the data. The write
    is the `inc` pattern from 0: pointer 0, then the bytes 1 to `length`.
    Given bytes 1 to the length of the inc pattern
    When the loop is opened
    Then a write of the inc pattern of the length and one more byte sends them all
    And a write of 0x00 to the target with next=restart answers "complete"
    And a read of the length from the target gives those bytes
    And those bytes are in the target's registers from 0

  @requires_option:i2c
  Scenario: Writes and reads of every length arrive whole
    NBYTES/RELOAD: writes and reads of every length, checked by CRC at both ends.
    When the loop is opened with the target in sink mode
    And the target is set to answer reads with PRBS data seeded with the length
    Then a write of the length of PRBS data with seed 7 sends the length and answers "complete"
    And the target received the length with the CRC of PRBS data of the length with seed 7
    And a read of the length with out=crc answers "complete" with the length and the CRC of PRBS data of the length seeded with the length

  @requires_option:i2c
  Scenario: A repeated START reads back the register
    `next=restart`: write the register, read it back after a repeated START, one STOP for both.
    When the loop is opened
    And the target's registers from 0x20 are set to 0xDE 0xAD 0xBE 0xEF
    And the master writes the pointer 0x20 with next=restart
    Then a read of 4 bytes from the target gives 0xDE 0xAD 0xBE 0xEF
    And the target counts 1 write, 1 read and 1 STOP

  @ad3 @requires_option:i2c
  Scenario: The repeated START is on the bus
    When the loop is opened at 100000 Hz
    And the logic analyser DIOs of the SCL and SDA pins of the master
    And the target's registers from 0x20 are set to 0x5A
    And the logic analyser is armed for 30 ms of the bus at 100000 Hz
    And the master writes the pointer 0x20 with next=restart
    And the master reads 1 byte from the target
    Then the logic analyser decodes at least 2 transfers within 2 s
    And the first writes 0x20 without a STOP
    And the second is a read after a repeated START that gives 0x5A, ended by a STOP

  @requires_option:i2c
  Scenario: A continued write is one transaction
    `next=continue`: the next write continues the same transaction without a START or an address.
    When the loop is opened
    Then a write of 0x30 0x01 0x02 to the target with next=continue answers "complete"
    And a write of 0x03 0x04 to the target answers "complete"
    And the target counts 1 write and 1 STOP, and its last transfer is 0x30 0x01 0x02 0x03 0x04
    And the target's registers from 0x30 hold 0x01 0x02 0x03 0x04

  @requires_option:i2c
  Scenario: A continued read is one transfer
    B.1d: a read with `next=continue` completes (RELOAD set) and the next read continues the same transfer.
    When the loop is opened
    And the bytes 0 to 7 are set in the target's registers from 0x40
    And the master writes the pointer 0x40 with next=restart
    And the master reads 3 bytes with next=continue, then 5 bytes
    Then both reads answer "complete"
    And together they give the bytes 0 to 7
    And the target counts 1 read and 1 STOP

  @requires_option:i2c
  Scenario: A clock-stretched transfer completes
    The target holds SCL low for `us` at the address phase (position 0) or at a byte: the transfer completes. A
    read's pointer is written first, unstretched and with a STOP, so the read is a transfer of its own.
    Then the transfer in the direction, stretched by the target for the stretch time at the position, completes

  @ad3 @requires_option:i2c
  Scenario: The clock stretch is on the bus
    The longest SCL low phase of the stretched transfer alone is at least the stretch (5 % slack for the
    Stopwatch).
    Given the logic analyser DIO of the SCL pin of the master
    When the transfer in the direction, stretched by the target for the stretch time at the position, completes with the logic analyser armed right before it on a falling SCL for the stretch and 2 ms
    Then the longest SCL low phase, captured within 2 s, is at least 95 % of the stretch time

  @requires_option:i2c
  Scenario: A misplaced STOP is a bus error, and the instance recovers
    B.1e: the target turns byte 2 of a read into a misplaced STOP: `buserror` after `EVT hook=buserror`, then a
    good read on the same open instance.
    When the loop is opened at the fault frequency with the target in sink mode
    And the target is set to turn byte 2 of a read into a STOP
    Then a read of 8 bytes from the target answers "buserror"
    And the master reports the "buserror" hook
    And the inc pattern of 8 bytes is what a read of 8 bytes from the target gives

  @requires_option:i2c
  Scenario: An idle I2cStm does not answer the general call
    B.1i: an idle I2cStm (the EEPROM adapter's, own address disabled) on the bus does not answer the general call,
    so the write is NACKed and the bus stays usable.
    When the EEPROM adapter is attached on the instance and the pins of the target
    And the instance of the master is opened on its pins
    Then a general call write of 0x00 from the master answers "nack"
    And a zero-length write from the master to 0x50 answers "complete"

  @requires_option:i2c
  Scenario: Close releases a bus the master holds
    `next=continue` leaves SCL held low by the master; `i2c.close` releases both lines (regression check, A.3) and a
    new open transfers normally.
    When the loop is opened
    Then a write of 0x00 0x01 to the target with next=continue answers "complete"
    When the master is closed
    Then unless the run is against the fake, the SCL and SDA pins of the master each read 1 as inputs
    When the master is opened again on its pins
    Then a write of 0x00 0x02 to the target answers "complete"

  @requires_option:i2c
  Scenario: Each instance is master against the other as target
    Each instance as master against the other as target.
    Then each instance, as master against the other as target at the target's address, writes 0x10 0xA5, reads 0xA5 back after writing the pointer 0x10 with next=restart, then both close
