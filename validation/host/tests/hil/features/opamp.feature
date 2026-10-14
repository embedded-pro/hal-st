@family:stm32g474
Feature: Operational amplifiers
  The operational amplifiers (`hal::OpampStm`) through the `opamp` group: the follower and the PGA against W1 and
  against the internal DAC, the output read by ADC1 (the internal VOPAMP1 channel of OPAMP1, the VOUT pin PB1 of
  OPAMP3), the output swing, the calibration trims, and the pins and DAC shared with the other groups. bundle1 or
  bundle2 for W1 on PA1 (OPAMP1 VINP0, OPAMP3 VINP2); bundle1 for PB1 on DIO10. The DAC cases need no wiring. Levels,
  gains and tolerances are in tests.opamp of the board file.

  @ad3
  Scenario: The follower tracks W1
    Given OPAMP1 is open as a follower of W1 with its internal output
    When W1 is set to the level
    Then the ADC reads the level within the tolerance

  @ad3
  Scenario: The PGA multiplies W1 by its gain
    Given OPAMP1 is open as a PGA of the gain on W1 with its internal output
    When W1 is set to the level
    Then the ADC reads the level times the gain within the tolerance

  Scenario: The internal DAC drives the follower and the PGA
    Given OPAMP1 is open on the internal DAC with the mode and gain of the case
    When the DAC is set to the code of the case
    Then the ADC reads the model output of the DAC level within the tolerance

  @ad3
  Scenario: OPAMP3 drives its output pin
    Given OPAMP3 is open as a follower of W1 with its output on PB1
    When W1 is set to the level
    Then the ADC reads the level within the tolerance
    And the output DIO reads the logic level of the level

  @ad3
  Scenario: The output stops at the rail
    Given OPAMP1 is open as a PGA of gain 64 on W1 with its internal output
    When W1 is set to the rail test level
    Then the ADC reads at least the rail level of the board file

  Scenario: Self-calibration and factory trims are in range
    Then OPAMP1 opened with and without self-calibration answers trims within range

  Scenario: The pins and the DAC of an amplifier are held while it is open
    When OPAMP1 is open on the internal DAC
    Then comp.open of comparator 1 on the same DAC channel answers busy
    And opamp.open of OPAMP3 answers busy while OPAMP1 is open
    When OPAMP1 is closed
    Then OPAMP1 opens on its VINP0 pin again

  Scenario: Malformed and out-of-range commands are refused
    Then the command line fails with the reason
