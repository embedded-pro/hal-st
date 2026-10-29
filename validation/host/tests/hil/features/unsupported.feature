Feature: Unsupported groups
  The groups hal-st has no driver for on these boards (Ethernet) and the groups the running MCU lacks (among them CAN
  on the STM32WB55 and STM32WBA55) answer `ERR unsupported`. The command lines are tests.unsupported.commands of the
  board file.

  Scenario: Commands of unsupported groups are refused
    Then the command line fails with "unsupported"
