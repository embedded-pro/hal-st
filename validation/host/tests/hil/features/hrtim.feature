@family:stm32g474
Feature: High-resolution timer
  The HRTIM timing units (`hal::HighResolutionPwmStm`, a `hal::SingleChannelPwm` and `hal::TwoChannelsPwm`) through
  the `hrtim` group, whose `duty`, `freq` and `stop` work as those of `pwm`: the prescaler and period of a frequency,
  both outputs in phase with one or two duties, complementary with dead time or shifted by a phase, the fault input
  FLT1, the ADC trigger, and the pins held while a unit is open. Wiring set `bundle1`: unit A on PA8/PA9
  (DIO4/DIO5), FLT1 on PA12 (DIO11, driven by the AD3). The cases without an AD3 open unit A on PA8/PA9 too, and
  those that set a duty run it. Frequencies, duties, phases, dead times and tolerances are in tests.hrtim of the
  board file; the expected values come from the driver's arithmetic in `hal_st_validation.groups.hrtim`.

  Scenario: The prescaler and period follow the frequency
    Then opening unit A at the frequency answers the timer clock and the prescaler ratio and period of the model

  Scenario: The frequency limits of the counter open and change, one step beyond is refused
    Then opening unit A one below the lowest or one above the highest frequency fails with "range"
    When unit A is opened at the lowest frequency
    Then changing the frequency to the highest answers the timing of the model
    And changing the frequency to one above the highest fails with "range"

  Scenario: The dead time picks the finest prescaler that holds it
    Then opening unit A complementary with the dead time answers the dead-time prescaler and ticks of the model

  Scenario: The duty and the frequency change on a running unit
    When unit A is opened at the first of the frequency changes
    Then every duty change answers the compares of the model for both outputs
    And every frequency change answers the timing of the model and keeps the duty

  Scenario: Two duties need both outputs in phase, a phase needs the shifted outputs
    When unit A is opened complementary
    Then two duties answer "usage" and one duty answers its compare
    When unit A is reopened with output 1 only
    Then two duties answer "usage"
    When unit A is reopened shifted
    Then a phase of 100 % answers "range" and one of 99.99 % succeeds

  @ad3
  Scenario: Both outputs run at the frequency and duty
    Given the outputs of unit A are wired
    When unit A opens in phase at the frequency and starts with the duty
    And both outputs are recorded
    Then both outputs run at the frequency of the reported period and the duty of the reported compare

  @ad3
  Scenario: Both outputs in phase run at their own duty
    Given the outputs of unit A are wired
    When unit A opens in phase at the two-duty frequency and starts with both duties
    And both outputs are recorded
    Then each output runs at the duty of its reported compare and both rise together

  @ad3
  Scenario: The complementary outputs switch over after the dead time
    Given the outputs of unit A are wired
    When unit A opens complementary at the complementary frequency with the dead time and starts
    And both outputs are recorded at the dead-time resolution
    Then the outputs are never active together
    And each switch-over waits the reported dead time

  @ad3
  Scenario: Output 1 follows output 2 by the phase
    Given the outputs of unit A are wired
    When unit A opens shifted at the phase frequency, gets the phase and starts
    And both outputs are recorded at the dead-time resolution
    Then the rising edges of output 1 follow those of output 2 by the phase

  @ad3
  Scenario: A fault forces the outputs inactive until the unit starts again
    As the break input of `pwm` without automatic output enable: a fault holds the outputs low, and only `Start`
    (`hrtim.duty`) enables them again.
    Given the outputs of unit A and FLT1 are wired
    And FLT1 is driven high
    When unit A opens in phase with FLT1 active low and starts
    Then both outputs switch and no fault is active
    When FLT1 is driven low and then high again
    Then a fault is active and both outputs stay low
    When the duty is set again
    Then both outputs switch and no fault is active

  Scenario: The ADC converts on the trigger of the unit period
    When unit A opens with one output at the ADC frequency with the ADC trigger and starts
    Then hrtim.adc counts conversions at the frequency of the reported period

  Scenario: The ADC count needs a trigger ADC1 takes and a running unit
    ADC1 converts on HRTIM triggers 1, 3, 5, 6, 7, 8, 9 and 10 only; a unit that does not run triggers nothing.
    When unit A is opened with one output and ADC trigger 2
    Then hrtim.adc answers "unsupported"
    When unit A is reopened with ADC trigger 1 and not started
    Then hrtim.adc answers "timeout"

  Scenario: The pins of a unit are held while it is open
    When unit A is opened with FLT1
    Then gpio.cfg of its output and fault pins answers busy
    And hrtim.open of unit B answers busy while unit A is open
    When unit A is closed
    Then gpio.cfg of its output pins succeeds

  Scenario: Commands that need an option or another mode are refused
    When unit A is opened in phase without fault input or ADC trigger
    Then the command line on the open unit fails with the reason

  Scenario: Malformed and out-of-range commands are refused
    Then the command line fails with the reason
