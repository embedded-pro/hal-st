Feature: ADC
  `hal::AdcStm` with `hal::AdcDmaMultiChannelStmBase`: wavegen DC levels against raw 12-bit codes.

  Wiring set `bundle1`: W1/W2 on the two inputs of `tests.adc.inputs`; the scope on the same pins measures the actual
  level, which then replaces the programmed one as reference. Without `timer` each run is software triggered; with
  `timer` the TRGO of that timer triggers the runs at `rate` per second. The ADC, its inputs, the sample count, the
  tolerances and the trigger settings are in tests.adc of the board file.

  @ad3
  Scenario: The ADC reads every DC level as its code
    Given the pin is driven to the level, measured by the scope where it is wired
    When the ADC is opened on the pin with the trigger
    Then the samples, as many as the sample count, read the level as its code within the tolerance and the spread

  @ad3
  Scenario: A sequence converts its pins in the given order
    One conversion per pin in the given order: the driven inputs alternate, interleaved with unwired spare inputs
    whose codes are not checked.
    Given the first input is driven to 0.8 V and the second to 2.4 V, each measured by the scope where it is wired
    And a sequence of the length alternates the two inputs and takes the next spare input every third position
    When the ADC is opened on the sequence with the trigger
    And the ADC measures the sample count of runs, at most as many as fit for the length
    Then there is one sample per pin of the sequence in every run
    And every driven pin of the sequence reads its level as its code within the tolerance and the spread

  @ad3
  Scenario: The ADC reads mid-supply as its code with every sampling time
    Given the first input is driven to 1.65 V, measured by the scope where it is wired
    When the ADC is opened on the first input with the sampling time and the trigger
    Then the samples, as many as the sample count, read the level as its code within the tolerance and the spread

  Scenario: Timer-triggered runs arrive at the rate
    Timer-triggered runs arrive at `rate` per second: `adc.measure n=<runs>` takes (runs - 1) / rate longer than
    `n=1` (`expect.adc_measure_time`), and the difference cancels the command round trip.
    Given the run count of the board file, capped at what fits for one pin and at half a second of runs at the rate (at least 2)
    When the ADC is opened on the first input, triggered by the timer at the rate
    And a single run is timed: the shortest round trip of the repeats less the transmission of its samples
    And the runs are timed the same way, less the single run
    Then the extra runs took their time at the rate within the rate tolerance and the jitter

  Scenario: adc.measure gives up after 1000 ms
    `adc.measure` gives up after 1000 ms: two runs at one per second answer `ERR timeout`.
    When the ADC is opened on the first input, triggered by the trigger timer at 1 run per second
    Then measuring 2 runs with a 3 s command timeout fails with "timeout"

  Scenario: A timer that triggers the ADC is busy for PWM and the encoder
    A timer serves one group at a time: while it triggers the ADC, PWM and the encoder get `ERR busy`.
    Given the first encoder of the board file on a timer that can trigger the ADC
    When the ADC is opened on the first input, triggered by the timer of the encoder
    Then opening PWM on that timer with channel 3 fails with "busy"
    And opening the encoder fails with "busy"

  Scenario: The ADC refuses a timer that cannot trigger it
    Then opening the ADC on the first input, triggered by the timer, fails with "unsupported"

  Scenario: Malformed and out-of-range opens are refused
    Then every malformed or out-of-range open of the ADC on the first input fails with its reason
    And opening another ADC number on the first input fails with "range"

  Scenario: adc.measure needs an open ADC, one ADC is open at a time, and the run count is limited
    Then measuring the ADC fails with "notopen"
    When the ADC is opened on the inputs
    Then opening the ADC on the inputs again fails with "busy"
    And measuring as many runs as fit gives one sample per input in every run
    And measuring 0 runs, one more than fit or more than the value limit fails with "range"

  Scenario: A GPIO on the pin blocks the ADC
    When the first input is configured as a GPIO input
    Then opening the ADC on the first input fails with "busy"

  Scenario: An ADC pin blocks a GPIO
    Analog users share a pin (a sequence may repeat it); a GPIO cannot take it.
    When the ADC is opened on a sequence of the first input twice
    Then configuring the first input as a GPIO input fails with "busy"
