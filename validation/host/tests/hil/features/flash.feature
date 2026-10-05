Feature: Internal flash
  Internal flash through the `flash` group, over the scratch region of the board (absolute pages 64-143 on STM32WB55, 64-127 on STM32WBA55):
  `hal::FlashHomogeneousInternalStm` / `hal::FlashInternalStm` (`variant=async`), their synchronous twins
  (`variant=sync`) and on STM32WB55 `hal::FlashCoordinatedWithWirelessStack` (`variant=coord`) over the async driver.

  `layout=homogeneous` has one sector per page; `layout=table` has single pages first and the page pattern 1, 1, 2, 4
  twice at the end (`groups.system_ext.table_sectors`). The firmware accepts erases and writes only from the sector
  `first` on: an unfixed erase (B.5) takes the sector index for the absolute page, which from there on cannot reach the
  running image. Every write targets flash words (8 bytes on STM32WB55, 16 on STM32WBA55) no earlier write programmed
  since the last erase; programming one twice would fail the driver's assertion, so the firmware answers `ERR failed`.

  The coordinated driver waits for HSEM 7 (CPU2 holds it while it needs the flash), holds its steps while the wireless
  stack is starting (`flash.stack starting` until `fus`), refreshes the watchdog it shares with `wdt` and excludes
  `hsem.lock`. No wiring. The region, the page and word sizes, the variants, the layouts, the erase time bounds, the
  HSEM hold, the transfer timeout and the watchdog timeout are in tests.flash of the board file.

  Scenario: The geometry agrees with the board file and the layout in every variant
    The region, its sectors and the first accepted sector agree with the board file and the layout rule; every
    variant reports the same geometry.
    Given the geometry of the layout
    Then its base and size are those of the scratch region of the board file
    And it has one sector per sector of the layout
    And it reports the layout
    And the image ends no later than the first page of the scratch region
    And the first accepted sector is the sector of the image, at most the sector count
    And at least one sector is accepted
    And every variant reports the same base, sector count, size, first accepted sector and image

  Scenario: Erasing a sector erases exactly its pages
    Erasing sector r erases exactly its pages of the region (B.5): a marker in r disappears, the markers in the
    next sector and the content of the previous one stay. The table layout erases a 2- and a 4-page sector.
    Given the geometry of the layout
    Then for the first accepted 2- and 4-page sectors in the table layout, the first accepted single-page one otherwise, erased with the next sector and marked at its start, its end and the start of the next sector, erasing the sector with the variant erases it, keeps the marker of the next sector and leaves the previous accepted sector as it was

  Scenario: Writes of every shape read back exactly
    Aligned, unaligned, odd-length, page-crossing and long writes on distinct flash words read back exactly, the
    bytes around them stay erased, and every variant reads the same data.
    Given the geometry of the layout
    And the first accepted 2-page sector in the table layout, the first accepted single-page sector and the next one otherwise
    And an aligned, an unaligned, an odd-length and a page-crossing write and a 300-byte PRBS write with seed 11, on distinct flash words
    When the sectors are erased with the variant
    And the writes are made with the variant
    Then every variant reads back each write and the CRC-32 of the sectors with the writes and erased bytes elsewhere

  Scenario: A programmed flash word is refused
    A second write to a programmed flash word answers `ERR failed` (the driver would assert); a word next to it
    is still writable.
    Given the first accepted single-page sector of the homogeneous layout
    When the sector is erased
    And byte 5a is written at offset 1 of the sector
    Then writing 00 at offset 0 of the sector fails with "failed"
    And writing 0000 at the last byte of the first flash word of the sector fails with "failed"
    When byte a5 is written at the start of the second flash word of the sector
    Then the sector reads ff 5a, erased bytes up to the second flash word and a5

  Scenario: A synchronous erase takes the page erase time
    A synchronous erase blocks for the page erase time of the flash (`us` per page within the board's bounds).
    Given the first accepted single-page sector of the homogeneous layout and the next accepted one
    When the sector and the next one are erased
    Then the erase took twice the page erase time within its bounds

  Scenario: Malformed and out-of-range commands are refused
    Given the geometry of the homogeneous layout
    Then every malformed, out-of-range or unsupported command fails with its reason

  @family:stm32wb55
  Scenario: A coordinated step waits for the CPU2 semaphore
    While HSEM 7 (CPU2's flash request) is held, a coordinated step waits; it runs once the semaphore is freed
    (here by the `hold=` timer of `hsem.take`), which the HSEM interrupt reports to the driver.
    Given the first accepted single-page sector of the homogeneous layout
    And two flash words of PRBS data with seed 5
    When the sector is erased
    And the data is written to the sector if the job is an erase
    And process 1 takes HSEM 7 for the HSEM hold
    And the job runs coordinated: a write of the data or an erase of the sector
    Then the sector holds the data after a write and is erased after an erase
    And the job took at least the HSEM hold less the hold margin
    And HSEM 7 is free

  @family:stm32wb55
  Scenario: A starting wireless stack holds a coordinated write until FUS
    `flash.stack starting` (`WirelessStackStarting()`) holds a coordinated write: its `ERR timeout` goes out, the
    flash stays erased, and `flash.stack fus` (`FirmwareUpgradeServicesReady()`) lets it complete with `EVT flash`.
    Given the first accepted single-page sector of the homogeneous layout
    And one flash word of incrementing data from 0x33
    When the sector is erased
    Then while the wireless stack is starting, a coordinated write of the data answers "timeout" and leaves the sector erased, and then the stack reports FUS ready
    And the flash event of a write arrives within 2 s
    And the sector holds the data

  @family:stm32wb55
  Scenario: The coordinated driver and hsem.lock exclude each other
    The coordinated driver and `hsem.lock` both rely on the HSEM interrupt: they exclude each other (HSEM 0 of
    `ResourceAllocation`).
    Then while the wireless stack is starting, the command "hsem.lock 12" fails with "busy", and then the stack reports FUS ready
    And HSEM 12 locks

  @family:stm32wb55 @resets_board
  Scenario: The coordinated driver borrows the unstarted watchdog while the stack starts
    The coordinated driver borrows the unstarted WWDG: `wdt.start` answers `ERR busy` until it is given back.
    Then while the wireless stack is starting, the command "wdt.start 0 timeout=100" fails with "busy", and then the stack reports FUS ready
    And watchdog 0 starts with a 100 ms timeout and automatic feeding

  @family:stm32wb55 @resets_board
  Scenario: A coordinated erase refreshes the running watchdog
    With the watchdog running (`feed=auto`), a coordinated erase refreshes it around every step it runs with the
    interrupts masked, so the board does not reset.
    Given the first accepted single-page sector of the homogeneous layout and the next three accepted ones
    When watchdog 0 starts with the watchdog timeout and automatic feeding
    And the sector and the next three are erased coordinated
    And the watchdog events of 0.3 s are collected
    Then the board did not reset
    And the board answers ping
