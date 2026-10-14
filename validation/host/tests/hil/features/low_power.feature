Feature: Low-power mode
  Low-power mode through the `lpm` group: `hal::LowPowerModeStm::Enter` inside a window where only the wake line's
  EXTI interrupt and the scaffold timer (TIM17, 1 us ticks, also the safety timeout) are enabled and the SysTick tick
  is off. The marker pin is low while the core sleeps; `sleeps` (the `Enter` calls, about one per millisecond when
  WFI really sleeps) shows that it did.

  `deep` maps to Sleep on STM32WB/WBA (`LowPowerModeStm::Stop`), so it behaves like `sleep` and never calls the
  clock-restore callback (`restored=0`). On the STM32G474 it enters Stop, which stops the scaffold timer and the
  PLL, so `deep` answers `ERR unsupported` there and only `sleep` is in the board file. The wake edge comes from the
  AD3 on the wake pin (bundle1 `gpio0`); the logic analyzer sees the marker low before the edge and high right after
  it. The wake and marker pins, the modes, the
  edges, the latency, the delays, the timeouts, the logic analyzer rate and the unbonded pin are in tests.lowpower of
  the board file.

  @ad3
  Scenario: The core sleeps until the edge on the wake pin
    The core sleeps (marker low) until the edge on the wake pin; the marker rises within `latency_us` of it and `us`
    covers the time asleep. `sleeps` proves the WFI: a sleeping core returns from `Enter` once per TIM17 update (1 ms)
    plus once for the edge, a driver that never sleeps busy-polls some 10^5 calls in that window.
    Given the run does not use the fake AD3
    And the wake pin and the marker pin are wired to DIOs
    When the AD3 drives the wake DIO idle for the edge, the logic analyzer arms on the edge, the core enters the mode until the edge on the wake pin with the marker and the wake timeout, and after the wake delay the AD3 drives the edge; the wake DIO is released even if that fails
    Then the core woke by EXTI without restoring the clock
    And it slept at least half the wake delay and at most the wake timeout
    And it returned from Enter at least once and at most twice per millisecond asleep plus twice
    And the marker was low before the wake edge
    And the marker rose after the wake edge within the latency

  Scenario: Without an edge the scaffold timer ends the window
    Without an edge the scaffold timer ends the window: `ERR timeout` after `timeout` ms; the board then answers, and
    its tick runs again (`delay` lasts its time).
    Given the run does not use the fake firmware
    And no enabled option loads the wake pin or the marker pin
    Then entering sleep with the wake pin and the marker pin for the timeout fails with "timeout" after at least 90 % of the timeout
    And the board answers ping
    And a firmware delay of 100 ms lasts at least 90 ms

  Scenario: Entering low-power mode releases the pins
    `lpm.enter` frees the wake and marker pins and the scaffold timer, whatever its outcome.
    When the core enters sleep with the wake pin and the marker pin for the timeout, unless that fails with "timeout"
    Then the wake pin and the marker pin can each be configured as an input and released

  Scenario: Malformed, out-of-range and conflicting commands are refused
    Then every malformed or out-of-range lpm.enter command line fails with its reason
    And with the marker pin configured as a GPIO output, entering sleep with the wake pin and the marker pin for 1 ms fails with "busy", and the marker pin is released even if that fails
