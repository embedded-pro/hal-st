Feature: Unique device ID
  `hal::UniqueDeviceId`, `info uid=`: 96 bits read from UID_BASE, the same after a reset, not erased flash (all 0x00
  or all 0xFF), with the lot number bytes in printable ASCII (RM0434 / RM0493 "Unique device ID register (96 bits)":
  bytes 0-3 wafer X/Y, byte 4 wafer number, bytes 5-11 lot number). The lot number bytes are tests.uid.lot of the
  board file, bytes 5 to 11 unless set.

  Scenario: The UID has the documented layout
    When the UID is read
    Then it has 12 bytes
    And it does not read like erased or blank memory
    And its lot number bytes are printable ASCII

  Scenario: The UID reads the same twice
    Then the UID reads the same twice

  @resets_board
  Scenario: The UID survives a reset
    When the UID is read
    And the board resets
    Then the board answers ping
    And the UID reads as before
