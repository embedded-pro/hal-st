@family:stm32g474
Feature: Analog comparators
  EMIL's `comp` commands over `hal::ComparatorStm` (`hal::AnalogComparator`) and `hal::SynchronousComparatorStm`, and
  hal-st's `comp.dac`: the output against the internal thresholds (VREFINT fractions, DAC3/DAC4), hysteresis,
  polarity, edge interrupts, the output pin, the propagation delay and blanking by a timer compare output. bundle1:
  COMP1 INP PA1 on W1 with its output on PA11 (DIO7) and TIM2 CH3 on PA9 (DIO5) for the blanking; COMP2 INP PA7 (DIO8)
  and output PA12 (DIO11) and COMP4 INP PB0 (DIO9) and output PB1 (DIO10) driven by the pattern generator. Levels,
  margins, edge counts and limits are in tests.comp of the board file.

  @ad3
  Scenario: The output follows W1 against the internal threshold
    Given comparator 1 is open with the threshold and its output pin
    When W1 is set below the threshold by the margin
    Then the comparator and its output pin read 0
    When W1 is set above the threshold by the margin
    Then the comparator and its output pin read 1

  @ad3
  Scenario: The inverted output follows W1 the other way round
    Given comparator 1 is open with the threshold, inverted, with its output pin
    When W1 is set below the threshold by the margin
    Then the comparator and its output pin read 1
    When W1 is set above the threshold by the margin
    Then the comparator and its output pin read 0

  @ad3
  Scenario: The DAC sets the threshold
    Given comparator 1 is open against the internal DAC with W1 at the DAC level
    When the firmware searches the DAC code where the output changes
    Then the code is the W1 level in DAC codes within the tolerance

  @ad3
  Scenario: Hysteresis widens the window between the switching levels
    Given W1 ramps across the half VREFINT threshold of comparator 1 at every hysteresis setting
    Then the window between the rising and the falling switching level grows with the setting
    And every window is within the limits of its setting

  @ad3
  Scenario: Edges of the comparator output are counted
    Given comparator 2 is open with its edge interrupt and the handler type
    When the AD3 sends the pulses on the INP DIO at the frequency
    Then the edge count is the number of edges of that kind

  @ad3
  Scenario: The output follows the input within the propagation delay
    Given comparator 4 is open with its output pin
    When the logic analyzer records the INP and the output DIO while the AD3 pulses the INP
    Then every output edge follows its input edge within the maximum delay

  @ad3
  Scenario: Blanking masks the output while the timer compare output is high
    Given comparator 1 is open blanked by the TIM2 CH3 compare output with W1 above the threshold
    When TIM2 CH3 runs at the blanking frequency and duty
    Then the output pin is low while TIM2 CH3 is high and high while it is low

  Scenario: The synchronous comparator reads but refuses edge interrupts
    When comparator 1 is open as a synchronous comparator
    Then comp.read answers its output
    And comp.irq answers unsupported

  Scenario: The DAC of a comparator is set and owned while it is open
    When comparator 1 is open against the internal DAC
    Then comp.dac sets the code
    And comp.dac of another comparator answers notopen
    When comparator 1 is closed
    Then comp.dac answers notopen

  Scenario: A pin held by another group cannot be opened
    Given the INP pin of comparator 1 is held by the gpio group
    Then comp.open of comparator 1 answers busy

  Scenario: Malformed and out-of-range commands are refused
    Then the command line fails with the reason
