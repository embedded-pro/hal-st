Feature: Analog input
  `hal::AnalogToDigitalPinImplStm`, `hal::AnalogToDigitalInternalTemperatureStm`, `hal::AdcTriggeredByTimerWithDma`,
  PROTOCOL.md D.11: single conversions against wavegen levels, the temperature sensor, and TIM2-paced bursts on one
  driver over a 256-sample buffer (B.12: `Measure(n)` delivers exactly n samples in n / rate, also a second time on
  the same driver).

  Wiring set `bundle1`: W1/W2 and the scope on `tests.ain.pins` (the ADC inputs of `tests.adc`); the burst counts,
  the temperature and the argument checks need no AD3. The pins, levels, sampling times, temperature range, burst
  and ramp settings are in tests.ain of the board file; the ADC, its resolution and tolerance in tests.adc.

  @ad3
  Scenario: ain.read converts the pin once
    `ain.read` converts the pin once (`AnalogToDigitalPinImplStm`).
    Given the pin is driven to the level, measured by the scope where it is wired
    Then one conversion of the pin reads the level as its code within the tolerance

  @ad3
  Scenario: ain.read converts mid-supply with every sampling time
    Given the first pin is driven to 1.65 V, measured by the scope where it is wired
    Then one conversion of the first pin with the sampling time reads the level as its code within the tolerance

  Scenario: The temperature sensor reads room temperature in whole degrees
    The internal sensor through `__LL_ADC_CALC_TEMPERATURE` (whole degrees) is at room temperature with a long
    sampling time; with the driver's default, below the sensor's minimum (B.14), a code still arrives.
    When the temperature sensor is read with the temperature sampling time
    Then the reading is in whole degrees
    And it is within the temperature range
    And the temperature sensor read with the default sampling time gives a code above 0 and below full scale

  Scenario: A burst of n samples delivers exactly n in n / rate
    `Measure(n)` with n below the buffer delivers exactly n samples in n / rate; the unfixed driver fills the
    whole buffer (256 samples in 256 / rate).
    Given the first pin is unloaded
    And the sample count is below the burst buffer
    When the first pin is measured in a burst of the sample count at the burst rate, as statistics
    Then the burst delivered the sample count
    And it took the time of the sample count at the burst rate within the tolerance and the latency

  Scenario: A second burst on the same driver again delivers n samples in n / rate
    `repeat=2` measures twice on one driver: the second `Measure(n)` starts from the state the first left (the
    unfixed driver leaves the ADC enabled with conversions pending) and again gives n samples in n / rate.
    Given the first pin is unloaded
    When the first pin is measured in 2 bursts of the sample count at the burst rate, as statistics
    Then both bursts delivered the sample count
    And each took the time of the sample count at the burst rate within the tolerance and the latency

  @ad3
  Scenario: Every sample of a burst reads the driven level
    Given the pin is driven to 2.0 V, measured by the scope where it is wired
    When the pin is measured in a burst of 16 samples at the burst rate
    Then the burst holds 16 samples
    And every sample is the level as its code within the tolerance

  @ad3
  Scenario: A burst follows a wavegen ramp
    A wavegen ramp during the burst: the samples rise at the ramp's slope per TIM2 period (the window holds at
    most one wrap of the sawtooth, so the samples split into at most two rising pieces). The wavegen ramp runs at
    50 % symmetry: it rises from low to high in half its period, so its slope is twice span x frequency.
    Given the wavegen drives the ramp on the first pin
    And the host waits 0.05 s
    When the first pin is measured in a burst of the sample count of the ramp at the rate of the ramp
    Then the burst holds samples
    And the samples split at the wraps of the ramp into at most 2 rising pieces
    And the longest piece rises at the ramp's slope per TIM2 period within 30 %

  Scenario: One group at a time holds the ADC, and a PWM on TIM2 blocks the burst only
    One group at a time holds the ADC: while `adc` is open `ain.read` and `ain.burst` answer `ERR busy`. The
    burst also needs TIM2 and the ADC's DMA channel, so a PWM on TIM2 blocks it while `ain.read` works.
    Given the first two pins are unloaded
    When the ADC is opened on the second pin
    Then reading the first pin and the temperature sensor fails with "busy"
    And a burst of 16 samples at 1000 per second on the first pin fails with "busy"
    When the ADC is closed
    And PWM opens TIM2 on the pin of its first channel
    Then a burst of 16 samples at 1000 per second on the first pin fails with "busy" while PWM holds TIM2
    And reading the first pin succeeds
    When PWM closes TIM2
    Then a burst of 16 samples at 1000 per second on the first pin delivers 16 samples
    And the ADC opens on the second pin, triggered by TIM2

  Scenario: Malformed and out-of-range commands are refused
    Then every malformed or out-of-range ain.read and ain.burst fails with its reason
