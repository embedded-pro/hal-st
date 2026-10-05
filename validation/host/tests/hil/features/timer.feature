Feature: Timers
  Timers (`hal::FreeRunningTimerStm`, `hal::TimerWithInterruptStm`) through the `tim` group: update rate on the
  marker pin, interrupt counts, the free-running counter up and down, stop, and the timer shared with pwm, tpwm, qei
  and the timer-triggered ADC.

  Wiring set `bundle1`: `tests.timer.marker` (gpio0) on a DIO; the marker toggles once per update interrupt, so it runs
  at half the update rate `timclk / ((prescaler + 1) (period + 1))`. The timers, the update settings, the counting
  window and the tolerances are in tests.timer of the board file.

  @ad3
  Scenario: The marker runs at half the update rate
    The marker toggles on every update interrupt: it runs at half the update rate.
    Given the marker pin is wired to a DIO
    When the timer opens with the update prescaler and period, the interrupt mode and the marker pin
    Then it reports the timer clock of the board file
    When the timer starts
    Then the marker runs at half the update rate within the frequency tolerance

  Scenario: The interrupt count follows the update rate
    `irqs` follows the update rate; dispatched callbacks at or below 1 kHz are not lost.
    When the timer opens with the update prescaler and period and the interrupt mode
    And the timer starts
    And the counts are read over the counting window
    Then the interrupts counted match the update rate within the count tolerance and the interrupt latency

  Scenario: The free-running counter counts up or down without interrupts
    `irq=none` counts at `timclk / (prescaler + 1)`, down from `period` with `mode=down`, without interrupts.
    When the timer opens free-running without interrupts, counting the mode way, or fails with "unsupported" when it counts down and the timer counts up only
    And the timer starts, if it opened
    And the counts are read over the counting window, if the timer opened
    Then the counter moved the mode way at the timer clock over the free-running prescaler + 1 within the count tolerance and the latency, without interrupts, if the timer opened

  Scenario: Starting raises no update callback of its own
    `tim.start` raises no update callback of its own: the update flag that `HAL_TIM_Base_Init` leaves on some HAL
    versions is cleared, so a timer with an update period of many seconds has `irqs=0` right after it starts.
    When the timer opens with the free-running prescaler and period and the interrupt mode
    And the timer starts
    Then the timer has counted no interrupt

  @ad3
  Scenario: The timer counts the requested way after the encoder
    An encoder leaves its timer in encoder mode with CR1.DIR at its last direction, where DIR ignores writes:
    `tim.open` must still count the requested way. The encoder runs against `mode` before it closes.
    Given the encoder timer is one of the timers under test
    And the encoder inputs are wired to DIOs
    When the encoder opens with the resolution of the board file
    And the AD3 turns the encoder 10 cycles at 1000 Hz against the mode
    And the encoder closes
    And the timer of the encoder opens free-running without interrupts, counting the mode way
    And the timer of the encoder starts
    And the counts of the timer of the encoder are read over the counting window
    Then the counter moved the mode way

  Scenario: Stop holds the counter and start resumes it
    `tim.stop` freezes the counter and the interrupts; `tim.start` resumes; repeated start and stop are harmless.
    When the timer opens with the first interrupt update setting and immediate interrupts
    And the timer starts twice
    And the host waits the counting window
    And the timer stops twice
    And the counts are read
    Then interrupts have been counted
    When the host waits the counting window
    Then the counts read as before
    When the timer starts
    And the host waits the counting window
    Then more interrupts have been counted than before

  @ad3
  Scenario: Stop stops the marker
    Given the marker pin is wired to a DIO
    When the timer opens with the second interrupt update setting, immediate interrupts and the marker pin
    And the timer starts
    And the timer stops
    Then the marker stays still over the record periods at half the update rate

  Scenario: The marker pin is released on close
    The marker pin belongs to the timer while it is open and returns to the pool with `tim.close`.
    Given the marker pin is not loaded by an option
    When the timer opens with immediate interrupts and the marker pin
    Then configuring the marker pin as an input fails with "busy"
    When the timer closes
    Then reading the counts of the timer fails with "notopen"
    When the marker pin is configured as an input
    Then opening the timer with immediate interrupts and the marker pin fails with "busy"

  Scenario: Invalid open arguments are refused in the protocol order
    Argument errors in the protocol order: usage, range, pin, unsupported, then the update interrupt rate (range).
    Given the first timer under test
    And a timer with a period wider than 16 bits, if any
    And a timer with a 16-bit period
    Then every invalid open is refused with its reason in the protocol order
    And the timer with the wider period opens with a period of 0xFFFFFFFF without interrupts and closes, if there is one
    And the first timer opens with prescaler 0 and period 1 without interrupts and closes
    And starting, stopping, reading and closing the closed first timer fail with "notopen"
    And the malformed command lines are refused with "usage"
    And opening the first timer with an unknown pin alias fails with "pin"

  Scenario: One timer is open at a time
    Given the first two timers under test
    When the first of them opens without interrupts
    Then opening the second of them without interrupts fails with "busy"
    And opening the first of them again without interrupts fails with "busy"

  Scenario: A timer serves one group
    A timer serves one group: while `tim` holds it, pwm, tpwm, the encoder and the ADC trigger get `ERR busy`,
    and `tim` gets it while pwm holds the timer.
    When the timer opens without interrupts
    Then opening pwm on the timer with channel 1 fails with "busy"
    And opening tpwm on the timer with its first timer PWM pin fails with "busy"
    And opening the encoder on the timer fails with "busy", if the timer has one
    And opening the ADC triggered by the timer fails with "busy", if the timer can trigger it
    When the timer closes
    And pwm opens on the timer with channel 1
    Then opening the timer without interrupts fails with "busy"
