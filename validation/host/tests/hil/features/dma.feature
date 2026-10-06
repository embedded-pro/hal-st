Feature: DMA
  `hal::CircularTransmitDmaChannel`, PROTOCOL.md D.12: `dma.wave` writes one 32-bit BSRR word per pattern bit on
  every TIM2 update, memory and peripheral side 32 bits wide (B.4 on both MCUs), so the pin shows the pattern, least
  significant bit first, at `rate` bits per second.

  Wiring set `bundle1` or `bundle2`: `tests.dma.pin` on a DIO; the argument and sharing checks need no AD3. The pin,
  the wave matrix, the wave time (`ms`), the start delay, the record bits, the samples per bit and the tolerances are
  in tests.dma of the board file, the unbonded pins in tests.system and the TIM2 channel pins in tests.pwm.timers.

  @ad3
  Scenario: The pin shows the pattern repeated at the rate
    The LA sees `pattern` repeated at `rate` (TIM2 rounded to whole kernel clocks): each level lasts a whole
    number of bit times and the bits between the first and the last edge are a part of the repeated pattern.
    Given the DMA pin is wired to a DIO
    And the actual rate: the rate rounded to whole timer kernel clocks
    When the wave of the pattern runs at the rate for the wave time, at least long enough for the start delay and the recording, while the logic analyzer records the record bits at the samples per bit after the start delay
    Then dma.wave answered OK after the wave time
    And every level on the DIO lasts a whole number of bit times within the tolerance and the bits between the first and the last edge are a part of the repeated pattern
    And those bits run at the actual rate within the tolerance

  Scenario: Missing and out-of-range wave arguments are refused
    `rate` and `pattern` are required, `pattern` holds 1-32 bytes, `rate` 1-1000000 and `ms` 1-10000.
    Then a wave on the DMA pin without pattern, without rate, with pattern -, with odd hex, a pattern too long, rate 0, a rate too high, ms 0, ms too long, on two pins, on an unbonded pin, on an unknown pin or on the terminal TX pin fails with usage, usage, usage, usage, range, range, range, range, range, usage, pin, pin and busy

  Scenario: The wave shares TIM2 and its pin
    TIM2 paces the wave, so a PWM on TIM2 makes `dma.wave` busy; so does its pin held as a GPIO. Both are free
    again once the wave answered.
    Given the DMA pin is loaded by no option the test does not handle
    And the first channel pin of TIM2 among the PWM timers
    When the PWM opens TIM2 on that pin
    Then a wave of a5 at 1000 bit/s for 10 ms on the DMA pin fails with "busy", as the PWM holds TIM2
    When the PWM closes TIM2
    And the DMA pin is configured as a GPIO output
    Then a wave of a5 at 1000 bit/s for 10 ms on the DMA pin fails with "busy", as a GPIO holds the pin
    When the GPIO of the DMA pin is released
    Then a wave of a5 at 1000 bit/s for 10 ms on the DMA pin runs
    And the DMA pin is configured as a GPIO output
    And the PWM opens TIM2 on that pin
