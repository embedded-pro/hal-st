@family:stm32wba55
Feature: LPTIM PWM
  `hal::LpTimerPwmWithChannels<N>`, `hal::LpPwmChannelGpio` (STM32WBA55 only: LpTimerPwmStm is not built for
  STM32WB) through the `lptpwm` group: frequency and duty per channel, `SetPulse`, an unused channel and the argument
  checks.

  Wiring set `bundle1`: LPTIM1 CH2 (PA15) and LPTIM2 CH1/CH2 (PA11/PA1) on DIOs. The tests assert the duty that
  `lptpwm.duty` asks for (CCR = ARR * duty / 100, high while the counter is below CCR). The LPTIM drives the active
  level from the compare match to the end of the period, so the driver selects the low output polarity; a measured
  1 - duty (given in the assertion message) means the polarity is wrong. The LPTIM takes compare writes only while it
  is enabled, so the tests set duties after `lptpwm.start`. The instances, the prescaler, the period, the duties, the pulse and the
  tolerances are in tests.lptim_pwm of the board file.

  @ad3
  Scenario: Every wired channel runs at the update rate with the duty
    Frequency lptimclk / prescaler / (period + 1) and the duty of every wired channel.
    Given the channels of the instance with a pin are wired
    When the instance is opened on its pins with the prescaler and period
    Then it reports the LPTIM clock of the board
    When the instance starts
    And every wired channel gets the duty
    And the channels are recorded at the update rate of the period
    Then every wired channel runs at the update rate of the period with the duty

  @ad3
  Scenario: SetPulse sets the compare value and the shared auto-reload
    `SetPulse(on, period)`: the channel's compare value and the shared auto-reload.
    Given the first channel of the instance with a pin is wired
    When the instance is opened on its pins with the prescaler and period
    And the instance starts
    And the channel gets the compare value and period of the pulse
    Then the channel runs at the update rate of the pulse period with a high fraction of the pulse compare value over the pulse period plus one

  @ad3
  Scenario: Stop stops the outputs and start resumes them
    `lptpwm.stop` stops the outputs; start again resumes them; repeating either is harmless.
    Given the channels of the instance with a pin are wired
    When the instance is opened on its pins with the prescaler and period
    And the instance starts
    And the instance starts
    And every channel with a pin gets 50 % duty
    And the instance stops
    And the instance stops
    And the channels are recorded at the update rate of the period
    Then no wired channel has more rising edges than a quarter of the record periods
    When the instance starts
    And every channel with a pin gets 50 % duty
    And the channels are recorded at the update rate of the period
    Then every wired channel runs at the update rate of the period with 50 % duty

  Scenario: Invalid opens and commands are refused
    Argument errors in the protocol order: usage, range, pin.
    Given the first instance with a pin on channel 1
    Then every invalid open setting fails with its reason
    And opening without pins fails with "usage" and with an unknown pin alias with "pin"
    And opening each LPTIM the MCU lacks fails with "range"
    And duty, pulse, start, stop and close fail with "notopen"
    When the instance opens on its pins
    Then a duty on channel 3 or of 101 %, a pulse compare value beyond the period limit and a pulse period of 0 fail with "range"

  Scenario: One LPTIM PWM runs at a time and claims its pins
    Given the pins of the first instance are unloaded
    When the first instance opens on its pins
    Then opening the second instance on its pins fails with "busy"
    And opening the first LPTIM as a timer without interrupt fails with "busy"
    And configuring the first pin of the first instance as a GPIO input fails with "busy"
    When the first instance closes
    Then the first pin of the first instance can be configured as a GPIO input
