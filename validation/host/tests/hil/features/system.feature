Feature: System
  General commands, framing, pins and error semantics (no AD3 needed). The boot timeout, the debug LED, the reserved
  pins and UARTs, the unbonded and invalid pins, the command lines of missing and unsupported instances and the
  delays are in tests.system of the board file.

  Scenario: The board answers ping
    Then the board answers ping

  Scenario: The board info matches the board file
    When the board info is read
    Then it names the board of the board file
    And it reports the family of the board file
    And it reports the system clock of the board file, which is the sysclk of its clock tree
    And it reports one of the reset causes iwdg, wwdg, sw, lpwr, obl, bor, pin and unknown
    And it reports a unique device ID of 24 hex digits

  Scenario: The firmware and the board file have the same pin aliases
    `board.pins` and the board file's `pins` are the same alias table, in both directions.
    When the firmware lists its pin aliases
    Then every alias of the board file names the same pin in the firmware
    And the firmware has no alias the board file lacks

  Scenario: The board file uses generic aliases, including the terminal pins
    Then every alias of the board file is a generic alias
    And the board file has the aliases terminaltx and terminalrx

  Scenario: Every alias names its pin in commands
    Each alias names its pin in commands; the terminal pins and the debug LED stay reserved.
    Then every alias of the board file configures as a GPIO input, its pin reads and the alias releases, except that configuring an alias of a terminal pin or of the debug LED fails with "busy"

  Scenario: The reserved pins are busy
    SWD, the LSE crystal and BOOT0 cannot be opened.
    Then configuring the pin as a GPIO input fails with "busy"

  Scenario: The terminal pins and the debug LED are busy
    Then configuring each terminal pin and then the debug LED pin as a GPIO input fails with "busy"

  Scenario: A pin the package does not bond out is refused
    A pin the package does not bond out, or of a port the MCU lacks, is `ERR pin`.
    Then configuring the pin as a GPIO input fails with "pin"

  Scenario: A malformed pin name is refused
    `P<port><index>` without leading zero and index 0-15, or an alias of the board (case-sensitive).
    Then configuring the pin as a GPIO input fails with "pin"

  Scenario: The terminal UART is reserved
    Then opening the reserved UART fails with "busy"

  Scenario: Unknown commands and keys are refused
    Then the command line "no.such.command" fails with "usage"
    And the command "ping" with nosuchkey=1 fails with "usage"

  Scenario: A command on a missing instance is refused
    Then the command line fails with "range"

  Scenario: A command on an unsupported instance is refused
    Then the command line fails with "unsupported"

  Scenario: Malformed commands are refused
    Then the command with the arguments fails with the reason

  Scenario: Closing what is not open and opening what is open are refused
    Given the first of the SPI instances of the board file
    Then closing it fails with "notopen"
    When it is opened on its CLK, MOSI and MISO pins
    Then opening it on these pins again fails with "busy"
    And opening it on these pins again with baud=1 fails with "range", argument errors coming before busy
    And it closes

  Scenario: A delay lasts at least the requested time
    When the firmware delays for the time, timed on the host
    Then it took at least 95 % of the time

  @resets_board
  Scenario: A reset reports a software reset
    When the board resets
    Then the boot message names the board, the family and the system clock of the board file and the reset cause "sw"
    And the board answers ping
    And the board info reports the reset cause "sw"
