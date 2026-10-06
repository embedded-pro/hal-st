Feature: PWM
  `hal::PwmStm` / `SynchronousPwmStm`: waveforms, channels, complementary outputs with dead time, idle levels, the
  break input, frequency changes and the argument checks.

  Wiring set `bundle1`: the outputs of `tests.pwm.timers` (channel pins, complementary `npin`s and `brk` inputs) on
  DIOs. Frequencies and duties are compared with what the protocol asks for after quantisation to whole counter
  ticks (`expect.pwm_frequency`/`pwm_duty`). The timers, the matrices, the frequencies, the duties and the
  tolerances are in tests.pwm of the board file.

  @ad3
  Scenario: One channel runs at the set frequency and duty
    One channel: `ERR range` exactly where the period does not fit the counter, else frequency and duty.
    Given the first channel of the timer is wired
    And the duty does not round to a static level at the PWM clock of the prescaler, unless it is 0 or 100 % or the period does not fit the counter
    When the channel is opened with the frequency, mode, prescaler and sync, which fails with "range" exactly when the period does not fit the counter
    Then it reports the PWM clock of the prescaler, if it opened
    When the duty is set and the channel is recorded, triggered on it unless the duty is 0 or 100 %, if it opened
    Then the channel runs at the quantised frequency and duty, if it opened

  Scenario: The frequency limits of the counter open and change, one step beyond is refused
    The lowest and highest frequency that fit open and change; one step beyond is `ERR range`, in `pwm.open`
    and in `pwm.freq` (under 2 counter ticks, or beyond 16 bits, 32 bits on TIM2, half the period centre aligned).
    Given the lowest and highest frequency that fit the counter of the timer at the PWM clock of the prescaler in the mode
    Then opening the first channel one below the lowest or one above the highest frequency fails with "range"
    When the first channel is opened at the lowest frequency
    Then it reports the PWM clock of the prescaler
    When the frequency changes to the highest
    Then changing the frequency to one below the lowest or one above the highest fails with "range"
    When the timer closes

  @ad3
  Scenario: The channels run with their own duties and common edges or centres
    1-4 channels of one timer with their own duties (in the order of the command) and common edges or centres.
    Given as many channels of the timer as the channel count are wired
    When the channels are opened in reverse order at the channels frequency with the mode and sync
    And the channels get the channel duties, in reverse order like their pins, and are recorded, triggered on the first channel
    Then every channel runs at the quantised frequency and duty of its channel duty
    And the channels line up on their pulse centres in center mode and on their rising edges otherwise

  @ad3
  Scenario: The channel and the complementary output switch over after the dead time
    Channel and complementary output never active together; each switch-over waits the dead time (counted in
    timer kernel clocks, saturating at the largest DTG value); `inv`/`invn` are undone before comparing.
    Given the first channel of the timer and its complementary output are wired
    When the channel and its complementary output are opened at the complementary frequency with the mode, dead time, inversion and sync
    And the duty is set to 40 %
    And both outputs are recorded
    Then the outputs with the inversion undone are never active together, for one sample at most when they switch on the same edge
    And each switch-over waits the dead time in timer kernel clocks
    And the high times of both outputs plus two dead times add up to one period

  @ad3
  Scenario: The complementary output runs alone while the channel pin stays a GPIO
    `-:<npin>` drives the complementary output only (the complement of the channel's reference, as the driver
    enables both outputs); the channel pin stays a GPIO driven low.
    Given the first channel of the timer and its complementary output are wired
    And the channel pin is configured as a GPIO output
    When only the complementary output is opened at the channels frequency
    And the duty is set to 30 %
    And the complementary output is recorded, triggered on it
    Then the channel pin stays low without an edge
    And the complementary output runs at the quantised edge-aligned frequency with the complement of the quantised 30 % duty

  Scenario: Dead times beyond 1 ms are refused
    `dead` up to 1 ms opens (saturating at the largest DTG value); beyond it `ERR range`.
    Given the first timer whose first channel has a complementary output
    Then opening that channel and its complementary output with the dead time fails with "range" exactly when the dead time does not fit

  @ad3
  Scenario: The outputs rest at the idle levels while disabled
    `idle`/`idlen` are the levels of the output and the complementary output while the outputs are disabled; the
    timer never drives both to their active level, so `idle=1 idlen=1` leaves both low (RM0434, break function).
    Given the first channel of the timer and its complementary output are wired
    When the channel and its complementary output are opened at the channels frequency with the idle levels
    And the duty is set to 50 %
    And both outputs are recorded
    Then both outputs switch before the stop
    When the timer stops
    And both outputs are recorded
    Then neither output switches after the stop
    And the output and the complementary output end at the idle levels, both low where both idle levels are high

  @ad3
  Scenario: The break input disables the outputs
    The break input at its active level (`brkpol`) disables the outputs; with `brkauto=1` they come back when
    it releases, otherwise they stay off until the next `pwm.duty`.
    Given the first channel of the timer and its break input are wired
    And the break input is driven to the inactive level of the break polarity
    When the channel is opened at the channels frequency with the break input, the break polarity and brkauto
    And the duty is set to 50 %
    Then the channel switches while the break input is inactive
    When the break input is driven to the active level of the break polarity and the break settle time passes
    Then the channel does not switch during the break
    When the break input is driven to the inactive level of the break polarity and the break settle time passes
    Then the channel switches again exactly when brkauto is set
    When the duty is set to 50 %
    Then the channel switches again after pwm.duty

  @ad3
  Scenario: The channel follows frequency changes and stops with the timer
    Given the first channel of the timer is wired
    When the channel is opened at the first of the frequency changes with the mode and sync
    And the duty is set to 50 %
    Then the channel runs at the quantised frequency and 50 % duty after each of the frequency changes
    When the timer stops
    Then the channel does not toggle at the last of the frequency changes

  Scenario: Invalid opens and commands on a closed timer are refused
    Given the first channel of the first timer
    Then every invalid open setting fails with its reason
    And opening the channel pin with two complementary outputs in one entry fails with "usage"
    And pwm.duty, pwm.freq, pwm.stop and pwm.close fail with "notopen"

  Scenario: What the timer lacks is refused
    What the timer lacks is `ERR unsupported`: centre alignment on TIM16/TIM17, channels beyond the timer's,
    CH4N, and complementary outputs, dead time, idle levels and the break input on timers without a break function.
    Then every open setting the timer lacks fails with "unsupported"

  Scenario: Malformed duties are refused
    Given the first timer with two channels is opened on its first two channels
    Then every malformed duty list fails with "usage"
    And the duties "12.5" and "100" are accepted
    And the duty "0.0001" is accepted

  Scenario: One timer runs at a time
    Given the first timer is opened on its first channel
    Then opening the first or the second timer on its first channel fails with "busy"

  @ad3
  Scenario: A reopen forgets the dead time, the break input and the idle levels
    Each open rebuilds the timer: a previous open's dead time, break input and idle levels do not survive.
    Given the first timer with a break input whose first channel has a complementary output
    And its first channel, the complementary output and the break input are wired
    When the break input is driven high
    And the channel and its complementary output are opened at the complementary frequency with a dead time of 2000 ns, the break input active low and both idle levels 1
    And the duty is set to 40 %
    And the timer closes
    And the break input is driven low
    And the channel and its complementary output are opened at the complementary frequency
    And the duty is set to 40 %
    And both outputs are recorded
    Then both outputs switch despite the previous break settings
    And the switch-overs wait no dead time
