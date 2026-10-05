Feature: Timer PWM
  Timer PWM (`hal::TimerPwmWithChannels<N>`, `hal::PwmChannelGpio`) through the `tpwm` group: frequency and duty of
  every channel, per-channel duties, `SetPulse` rewriting the period of every channel, unused (`-`) channels, starting
  and stopping one channel or all, and the argument checks.

  Wiring set `bundle1` (WBA55 TIM2 with CH3/CH4: `bundle2`): the channel pins of `tests.timer_pwm.timers` on DIOs.
  `SetDuty` writes CCR = ARR * duty / 100 and the output is high while the counter is below CCR, so 100 % keeps one low
  counter tick per period (`groups.timers.pwm_duty_fraction`); the YAML prescaler makes that tick 1 us long. The
  timers, the prescaler, the period, the duties, the pulse and the tolerances are in tests.timer_pwm of the board file.

  @ad3
  Scenario: Every channel runs at the update rate with the duty
    One duty on every channel: frequency timclk / ((prescaler + 1) (period + 1)), high fraction of `SetDuty`.
    Given the channel pins are wired to DIOs
    When the timer PWM opens with the prescaler and period of the board file
    Then it reports the timer clock of the board file
    When every wired channel is set to the duty
    And the timer PWM starts
    And the channels are recorded
    Then every wired channel runs at the update rate with the high fraction of the duty

  @ad3
  Scenario: Each channel keeps its own duty
    Each channel keeps its own duty: the channel number is the position in `pins`.
    Given the channel pins are wired to DIOs
    When the timer PWM opens with the prescaler and period of the board file
    And each channel is set to its channel duty of the board file
    And the timer PWM starts
    And the channels are recorded
    Then every wired channel runs at the update rate with the high fraction of its channel duty

  @ad3
  Scenario: A pulse sets the period of every channel
    `SetPulse(on, period)` writes the channel's compare value and the shared auto-reload: every channel changes
    frequency, the others keep their compare value.
    Given the channel pins are wired to DIOs
    When the timer PWM opens with the prescaler and period of the board file
    And every channel is set to 50 %
    And the timer PWM starts
    And channel 1 gets the pulse of the board file
    And the channels are recorded at the update rate of the pulse period
    Then channel 1 runs with the compare value of the pulse and every other wired channel with the compare value of 50 %, all at the update rate of the pulse period

  @ad3
  Scenario: An unused channel leaves its pin free
    A `-` channel gets a `DummyPinStm`: its pin stays free for other groups, the other channels run unchanged.
    Given the first used channel is left unused
    And the remaining channel pins are wired to DIOs
    When the timer PWM opens with those pins and the prescaler and period of the board file
    And every channel is set to 50 %
    And the timer PWM starts
    And the pin of the unused channel is configured as an input
    And the channels are recorded
    Then every wired channel runs at the update rate with the high fraction of 50 %

  @ad3
  Scenario: One channel or all start and stop
    `ch=` starts or stops one channel; without it every channel; repeating either is harmless.
    Given the channel pins are wired to DIOs
    And the first two wired channels
    When the timer PWM opens with the prescaler and period of the board file
    And every channel is set to 50 %
    And the first of them starts twice
    And the channels are recorded
    Then the first of them runs at the update rate with the high fraction of 50 %
    And the second of them, which was not started, does not run
    When the timer PWM starts
    And the first of them stops
    And the channels are recorded
    Then the first of them, which was stopped, does not run
    And the second of them runs at the update rate with the high fraction of 50 %
    When the timer PWM stops twice
    And the channels are recorded
    Then neither of them runs
    When the timer PWM starts
    And the channels are recorded
    Then the second of them runs at the update rate with the high fraction of 50 %

  @ad3
  Scenario: The break input left enabled by pwm is disabled
    `pwm.close` leaves BDTR with the break input enabled; with `brkpol=low` and the break pin back in analog mode
    that break stays active, so `tpwm` on the same timer only drives its outputs because it rewrites BDTR.
    Given a pwm break input on the timer
    And the channel pins are wired to DIOs
    When pwm opens on the first pwm channel of the timer with the break input active low
    And pwm closes on the timer
    And the timer PWM opens with the prescaler and period of the board file
    And every wired channel is set to 50 %
    And the timer PWM starts
    And the channels are recorded
    Then every wired channel runs at the update rate with the high fraction of 50 %

  Scenario: Invalid open arguments are refused in the protocol order
    Argument errors in the protocol order: usage, range, pin, unsupported.
    Given a single-channel timer of the board file
    And the timer with the most channels
    Then every invalid open of it is refused with its reason in the protocol order
    And the malformed open command lines of it are refused with their reason
    And opening the timer with the most channels with its channel pins reversed fails with "pin", if it has several and reversing them changes them and leaves a pin first
    And setting a duty or a pulse, starting, stopping and closing it fail with "notopen"

  Scenario: Invalid channel arguments are refused
    Given the timer with the most channels, its channel pins not loaded by an option
    When the timer PWM opens on it with its channel pins
    Then every out-of-range duty and pulse is refused with "range"
    And starting the channel after the last one fails with "range"
    And stopping channel 0 fails with "range"
    And the malformed channel command lines are refused with "usage"
    And the last channel takes a duty of 100 % and a pulse with the largest compare value and period

  Scenario: The timer PWM holds its timer against pwm and tim
    `tpwm` holds its timer against pwm and tim, and one timer at a time.
    Given the first two timers of the board file, the channel pins of the first not loaded by an option
    When the timer PWM opens on the first of them with its channel pins
    Then opening pwm on the first of them with channel 1 fails with "busy"
    And opening tim on the first of them without interrupts fails with "busy"
    And opening the timer PWM on the second of them with its channel pins fails with "busy"
    When the timer PWM closes on the first of them
    And tim opens on the first of them without interrupts
    Then opening the timer PWM on the first of them with its channel pins fails with "busy"

  Scenario: The channel pins are claimed while open
    The channel pins belong to `tpwm` while it is open and return to the pool with `tpwm.close`.
    Given the first timer of the board file, its channel pins not loaded by an option
    And its first channel pin
    When the timer PWM opens on it with its channel pins
    Then configuring its first channel pin as an input fails with "busy"
    When the timer PWM closes on it
    And its first channel pin is configured as an input
    Then opening the timer PWM on it with its channel pins fails with "busy"
