Feature: PWM extensions
  `hal::PwmStm`, PROTOCOL.md D.10: the five counter modes, compare preload, the break input filter, and the trigger
  output an `adc.open trgo=` sequence runs on.

  Wiring set `bundle1`: the outputs and break inputs of `tests.pwm.timers` on DIOs (`tests.pwm_ext` names the
  entries); the TRGO and argument tests need no AD3. The modes, the timers, the frequencies, the duties, the break
  filter cases, the TRGO matrix and the tolerances are in tests.pwm_ext of the board file.

  @ad3
  Scenario: Every mode gives its waveform and lines up the outputs of one timer
    Every mode gives the frequency and duty of its waveform; two outputs of one timer share their rising edges
    counting up, their falling edges counting down (`edgedown`) and their pulse centres centre aligned.
    Given the first two channels of the alignment timer are wired
    When they are opened at the alignment frequency in the mode
    And they get the alignment duties and are recorded, triggered on the first one
    Then both channels run at the quantised frequency and duty of the waveform of the mode
    And their rising edges line up counting up, their falling edges counting down and their pulse centres centre aligned

  @ad3
  Scenario: Counting down, 0 % keeps one counter tick high per period
    At 0 % `mode=edge` stays low, while `mode=edgedown` keeps one counter tick high per period: counting down,
    PWM mode 1 is active while CNT <= CCR, so CCR = 0 still matches once (PwmStm.hpp).
    Given the first channel of the alignment timer is wired
    And a prescaler that makes the counter tick last the zero-duty tick
    When the channel is opened in edge mode at the zero-duty frequency, set to 0 % duty, recorded over the zero-duty periods and closed
    Then it stays low without an edge
    When the channel is opened in edgedown mode at the zero-duty frequency, set to 0 % duty, recorded over the zero-duty periods and closed
    Then it is high for one counter tick in every period but two at most

  Scenario: The counter modes and trgo need a master timer
    Every mode but `edge` needs a counter mode select and `trgo` a master mode (both: TIM1, TIM2 and on
    STM32WBA55 TIM3); TIM16/TIM17 answer `ERR unsupported`, the others open.
    Then every mode but edge, trgo update and trgo oc1ref open on the first channel of the timer exactly when it has a counter mode select, else fail with "unsupported"

  @ad3
  Scenario: Compare preload holds a duty write until the next period
    Duty 80 % then 20 %, written `runt_repeats` times while the LA records: without preload a write lands
    mid-period and cuts or stretches that period's pulse (some high pulse lies strictly between 20 % and 80 %);
    with preload every period shows 20 % or 80 %.
    Given the first channel of the preload timer is wired
    When the channel is opened at the preload frequency with a 1 MHz counter and the preload
    And the duty is set to 20 %
    And the logic analyser records the runt window while the duty is set to 80 % and back to 20 % the runt repeats times
    Then the writes took less than the recording
    And pulses were captured
    And with preload no pulse lies strictly between 20 % and 80 % beyond the runt margin, without preload some pulse does

  @ad3
  Scenario: The break filter ignores a break pulse shorter than the filter
    `brkfilter` (BDTR.BKF) ignores a break pulse shorter than the filter: one AD3 pulse of `ticks` kernel clocks
    trips the break (outputs off, `brkauto=0`) exactly when it outlasts the filter; without a filter even the
    short pulse trips it.
    Given the break filter timer has a wired break input
    And its first channel and its break input are wired
    And the break filter case trips exactly when its pulse outlasts the filter
    When the break input is driven low
    And the channel is opened at the break frequency with the break input active high and the filter of the case
    And the duty is set to 50 %
    Then the channel switches before the pulse
    When the AD3 pulses the break input once for the ticks of the case and the break settle time passes
    Then the channel stops switching exactly when the case trips

  Scenario: The TRGO of the timer paces the ADC
    `adc.open trgo=<t>` converts once per TRGO of the timer the pwm group drives: with `trgo=update` or
    `trgo=oc1ref` once per period, so `adc.measure n=<runs>` takes (runs - 1) periods longer than `n=1`.
    Given the first ADC input and the first channel of the timer are unloaded
    When the channel is opened at the TRGO frequency with the TRGO source
    And the ADC opens on its first input, triggered by the TRGO of the timer
    And the duty is set to 50 %
    And the shortest round trips of one conversion and of the TRGO runs, less their replies, are measured
    Then the extra runs take one period each, within the TRGO tolerance and jitter

  Scenario: Invalid ADC trgo settings are refused
    `trgo` excludes `timer` and `rate` (`ERR usage`); a timer the MCU lacks is `ERR range`; a timer nobody drives,
    or one whose TRGO the ADC cannot trigger from, is `ERR unsupported`; one another group holds is `ERR busy`.
    Given the first ADC input and the first timer of the TRGO matrix
    Then every invalid ADC trgo setting fails with its reason
    And an ADC on the TRGO of each timer that has none fails with "unsupported" while the timer runs
    And an ADC on the TRGO of the timer fails with "busy" while its encoder holds it, if the timer has one

  Scenario: Invalid break filter, preload, mode and trgo settings are refused
    `brkfilter` 0-15 needs `brk` (`ERR usage`), `preload` is 0 or 1, `mode` and `trgo` take their names only.
    Given the alignment timer and its break input
    Then every invalid break filter, preload, mode and trgo setting fails with its reason
    And the timer opens on channel 1 with the break input, break filter 15, no preload, centerboth mode and trgo oc4ref
