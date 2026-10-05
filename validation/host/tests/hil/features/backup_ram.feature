Feature: Backup RAM
  Backup registers through the `bkp` group: `hal::BackupRamStm` used as `hal::BackupRam<volatile uint32_t>` (B.8:
  public inheritance; on STM32WBA55 the driver also clocks the TAMP registers and opens the backup domain).
  STM32WB55: RTC BKP0R-BKP19R; STM32WBA55: TAMP BKP0R-BKP15R. The words survive a system reset. No wiring. The
  number of words is tests.bkp.words of the board file.

  Scenario: The driver has the backup words of the board file
    Then the driver reports the number of words of the board file

  Scenario: Each word holds its own value
    Each word holds its own value: written one by one, all read back, none disturbed by its neighbours.
    When each word is written in turn with the pattern XORed with its index times 0x01010101
    Then all the words read back the values written

  Scenario: Fill and check agree on the fill values of the seed
    When the words are filled from the seed
    Then checking against the seed finds no word that differs
    And checking against the seed with bit 0 flipped finds every word different
    And word 0 reads the fill value of the seed

  @resets_board
  Scenario: The words survive a system reset
    The backup domain keeps the words through a system reset.
    When the words are filled from the seed 0x13579BDF
    And the board resets
    Then checking against that seed finds no word that differs
    And the last word reads the fill value of that seed

  Scenario: Malformed and out-of-range commands are refused
    Then every malformed or out-of-range bkp command line fails with its reason
