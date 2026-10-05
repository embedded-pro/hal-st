Feature: Clock
  The hal-st default clock configurations (`ConfigureDefaultClockNucleoWB55RG`, `ConfigureDefaultClockNucleoWBA55CG`)
  through the `clock` group: bus clocks, oscillator ready flags and the RNG kernel clock selection against
  `tests.clock`, and on STM32WB55 the clocks themselves on the MCO pin.

  MCO (STM32WB55 only; the only MCO pin of the STM32WBA55 is the terminal RX): `sgpio.af <mco pin> af=0` muxes the
  pin, `clock.mco` selects source and divider, and the logic analyzer measures the frequency with a least-squares fit
  of the rising edge times (`groups.io.edge_fit_frequency`), precise enough for the LSE crystal's +-100 ppm. The
  frequencies, flags and RNG clock are in tests.clock of the board file, the MCO pin, its outputs and tolerances in
  tests.clock.mco.

  Scenario: The bus clocks are those of the board file
    When the clock info is read
    Then every bus clock is its frequency of the board file
    And pclk7 is reported only where the board file has it
    And sysclk is that of the board file and of the system info
    And pclk1 and pclk2 are the clocks of the board file the expectations use

  Scenario: The oscillator ready flags and the RNG kernel clock are those of the board file
    When the clock info is read
    Then every oscillator ready flag is that of the board file
    And the RNG kernel clock selection is that of the board file
    And clk48 is that of the board file, none unless set

  @ad3
  Scenario: The MCO pin outputs the source of the output divided by its divider
    Given the MCO pin is wired to a DIO
    When the MCO pin is muxed to its alternate function
    And the MCO outputs the source of the output divided by its divider
    Then the logic analyzer measures the frequency of the output on the DIO within its tolerance

  @ad3
  Scenario: clock.mco off stops the MCO output
    Given the MCO pin is wired to a DIO
    When the MCO pin is muxed to its alternate function
    And the MCO outputs "sysclk" divided by 16
    And the MCO is turned off
    Then the logic analyzer records no edge of the MCO on the DIO in 1 ms

  @ad3
  Scenario: clock.hsi48 stops and restarts HSI48
    `clock.hsi48 0` stops HSI48 (no MCO edges, ready flag 0); `clock.hsi48 1` brings both back.
    Given the HSI48 output of the MCO
    And the MCO pin is wired to a DIO
    When the MCO pin is muxed to its alternate function
    And the MCO outputs "hsi48" divided by the divider of the HSI48 output
    And HSI48 is turned off
    Then the HSI48 ready flag is 0
    And the logic analyzer records no edge of HSI48 on the DIO in 1 ms
    When HSI48 is turned on
    Then the HSI48 ready flag is 1
    And the logic analyzer measures the frequency of the HSI48 output on the DIO within its tolerance

  Scenario: clock.info takes no argument
    Then the command line "clock.info 1" fails with "usage"
    And the command line "clock.info all=1" fails with "usage"

  Scenario: Malformed and out-of-range clock.mco and clock.hsi48 commands are refused, and every source is accepted
    Then every malformed or out-of-range clock.mco and clock.hsi48 command fails with its reason
    And the MCO outputs every MCO source in turn
    And HSI48 is turned on
