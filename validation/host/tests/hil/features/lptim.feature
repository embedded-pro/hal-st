Feature: Low-power timers
  Low-power timers (`hal::FreeRunningLowPowerTimerStm`, `hal::LowPowerTimerWithInterruptStm`) through the `lptim`
  group: update rate on the marker pin for every prescaler, the WBA repetition counter, interrupt counts, the
  free-running counter, stop, and the LPTIM shared with the LPTIM encoder (`qei lp=1`) and `lptpwm`.

  Wiring set `bundle1`: `tests.lptim.marker` (gpio0) on a DIO; the marker toggles once per update interrupt, so it runs
  at half the update rate `lptimclk / prescaler / (period + 1) / (rep + 1)`, with the LPTIM kernel clock `lptimclk` of
  the board file's `clocks.lptim` (`lptim.open` reports it, test_reports_the_kernel_clock). The instances, the update
  settings, the prescalers, the repetitions, the counting window and the tolerances are in tests.lptim of the board
  file.

  Scenario: Open reports the LPTIM kernel clock
    When the LPTIM opens without interrupts
    Then it reports the LPTIM kernel clock of the board file

  @ad3
  Scenario: The marker runs at half the update rate
    The marker toggles on every update interrupt: it runs at half the update rate.
    Given the marker pin is wired to a DIO
    When the LPTIM opens with the update prescaler and period, the interrupt mode and the marker pin
    And the LPTIM starts
    Then the marker runs at half the update rate of the LPTIM kernel clock within the frequency tolerance

  @ad3
  Scenario: Every prescaler divides the update rate
    Every divider of the LPTIM clock (1 to 128) divides the update rate.
    Given the marker pin is wired to a DIO
    When the LPTIM opens with the prescaler, the prescaler period of the board file, immediate interrupts and the marker pin
    And the LPTIM starts
    Then the marker runs at half the update rate of the prescaler and the prescaler period within the frequency tolerance

  @ad3
  Scenario: The repetition counter updates once every rep + 1 periods
    `rep` (WBA LPTIM): one update every `rep + 1` periods.
    Given the marker pin is wired to a DIO
    When the LPTIM opens with the repetition timing of the board file, the repetition count, immediate interrupts and the marker pin
    And the LPTIM starts
    Then the marker runs at half the update rate of the repetition timing and count within the frequency tolerance

  Scenario: The interrupt count follows the update rate
    `irqs` follows the update rate; dispatched callbacks at or below 1 kHz are not lost.
    When the LPTIM opens with the update prescaler and period and the interrupt mode
    And the LPTIM starts
    And the counts are read over the counting window
    Then the interrupts counted match the update rate within the count tolerance and the interrupt latency

  Scenario: The free-running counter counts without interrupts and stop freezes it
    `irq=none` counts without interrupts; `lptim.stop` freezes the counter; repeated start and stop are harmless.
    When the LPTIM opens free-running without interrupts
    Then the LPTIM has counted no interrupt
    When the LPTIM starts twice
    Then 5 readings of the counter show no interrupt, stay within the period and are not all the same
    When the LPTIM stops twice
    And the counts are read
    And the host waits the counting window
    Then the counts read as before

  @ad3
  Scenario: Stop stops the marker
    Given the marker pin is wired to a DIO
    When the LPTIM opens with the second interrupt update setting, immediate interrupts and the marker pin
    And the LPTIM starts
    And the LPTIM stops
    Then the marker stays still over the record periods at half the update rate

  Scenario: The repetition counter is there on the WBA only
    `rep` 0..255 on the WBA LPTIM; the STM32WB LPTIM has no repetition counter (`ERR unsupported`).
    Given the first LPTIM under test
    Then opening the first LPTIM with a repetition count past the largest fails with "range"
    And opening the first LPTIM with a repetition count of 0 fails with "unsupported", if the board file says it has no repetition counter
    And the first LPTIM opens with the largest repetition count and closes, if the board file says it has a repetition counter

  Scenario: Invalid open arguments are refused in the protocol order
    Argument errors in the protocol order: usage, range, pin.
    Given the first LPTIM under test
    Then every invalid open of the first LPTIM is refused with its reason in the protocol order
    And the first LPTIM opens with period 1 without interrupts and closes
    And opening each missing LPTIM of the board file fails with "range"
    And starting, stopping, reading and closing the closed first LPTIM fail with "notopen"
    And opening the first LPTIM counting down fails with "usage"
    And opening the first LPTIM with an unknown pin alias fails with "pin"

  Scenario: One LPTIM is open at a time
    Given the first two LPTIMs under test
    When the first of them opens without interrupts
    Then opening the second of them without interrupts fails with "busy"

  Scenario: The marker pin is released on close
    Given the first LPTIM under test
    And the marker pin is not loaded by an option
    When the first LPTIM opens with immediate interrupts and the marker pin
    Then configuring the marker pin as an input fails with "busy"
    When the first LPTIM closes
    Then the marker pin can be configured as an input

  Scenario: An LPTIM serves one group
    An LPTIM serves one group (`ResourceAllocation` lpTimer): the LPTIM encoder, `lptim` and `lptpwm`.
    Given the LPTIM PWM on the LPTIM of the encoder in the board file, if any
    When the LPTIM encoder opens
    Then opening the LPTIM of the encoder without interrupts fails with "busy"
    And opening the LPTIM PWM on it fails with "busy", if there is one
    When the LPTIM encoder closes
    And the LPTIM of the encoder opens without interrupts
    Then opening the LPTIM encoder fails with "busy"
    When the LPTIM of the encoder closes
    And the LPTIM PWM opens on it, if there is one
    Then opening the LPTIM of the encoder without interrupts fails with "busy", if there is an LPTIM PWM
