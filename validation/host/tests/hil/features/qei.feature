Feature: Quadrature encoder
  `hal::SynchronousQuadratureEncoderStm`, LPTIM `SynchronousQuadratureEncoderLpTimStm`, driven by the AD3 pattern
  generator.

  Wiring set `bundle1`: A, B and index of each `tests.qei.instances` entry on DIOs. The LPTIM encoders
  (`tests.qei.lp_instances`): NUCLEO-WB55RG LPTIM1 in `bundle2`; NUCLEO-WBA55CG LPTIM2 in `bundle1` and LPTIM1 in
  `bundle2`. The pattern generator produces an exact number of 4-state cycles (A leads B for `fwd`); the LPTIM counts
  both edges of both inputs (`cap=ab`) or the rising or falling edges only (`cap=rise|fall`, two counts per cycle).
  The encoders, the resolution, the counts per cycle, the speed tolerance and the parameter matrices are in tests.qei
  of the board file.

  @ad3
  Scenario: The count follows the capture mode, and inverting exactly one phase reverses it
    Counts per cycle follow the capture mode; inverting exactly one phase reverses the direction.
    Given the A, B and index inputs of the encoder are wired to DIOs
    When the encoder is opened with the capture mode and the inversions
    And the position of the encoder is read
    And the AD3 runs the cycles at the frequency in the direction
    Then the position moved by the counts of the cycles for the capture mode, direction and inversions, and the direction reads accordingly

  @ad3
  Scenario: Inverted inputs restore physically inverted phases
    Physically inverted phases with the matching `inva`/`invb` count like the plain signal.
    Given the A, B and index inputs of the encoder are wired to DIOs
    When the encoder is opened with the inversions
    And the position of the encoder is read
    And the AD3 runs 25 cycles at 1000 Hz with the phases physically inverted as the inversions
    Then the position moved forward by both edges of both inputs of the 25 cycles

  @ad3
  Scenario: The position starts at the offset, rolls over at the resolution and comes back
    Given the A, B and index inputs of the encoder are wired to DIOs
    When the encoder is opened with the resolution and the offset
    Then the encoder reads the offset as position and the resolution
    When the AD3 runs 30 cycles at 1000 Hz in direction "fwd"
    Then the position is the offset plus 4 counts for each of the 30 cycles, wrapped at the resolution
    When the AD3 runs 30 cycles at 1000 Hz in direction "rev"
    Then the position is the offset

  @ad3
  Scenario: The speed is in counts per second, sampled every velocity period
    `speed` is in counts per second, sampled every `vel` µs.
    Given the A, B and index inputs of the encoder are wired to DIOs
    When the encoder is opened with the velocity period and capture mode "ab"
    And the speed is read 50 ms after three velocity periods of an endless quadrature at the frequency, which then stops
    Then the speed is the counts per cycle of both edges of both inputs times the frequency, within the speed tolerance or one count per velocity period

  @ad3
  Scenario: qei.index reads the level of the index input, which never changes the count
    `qei.index` reads the level of the index input, which never changes the count.
    Given the A, B and index inputs of the encoder are wired to DIOs
    When the encoder is opened
    And the position of the encoder is read
    Then qei.index reads every level the DIO on the index input drives, and the DIO is released
    And the position is unchanged

  @ad3
  Scenario: The LPTIM encoder counts the edges of the capture mode, reversed by inva
    The LPTIM encoder (`lp=1`) counts both edges of both inputs (`ab`) or the rising or falling edges only;
    `inva=1` reverses the direction (mirrored mounting).
    Given the A and B inputs of the encoder are wired to DIOs
    When the LPTIM encoder is opened at its maximum resolution with the inversion, the filter and the capture mode
    And the position of the encoder is read
    And the AD3 runs the cycles at the frequency in the direction
    Then the position moved by the LPTIM counts of the cycles for the capture mode, the direction and the inversion, wrapped at the maximum resolution, and the direction reads accordingly

  Scenario: The default encoder opens on its default pins
    The default encoder opens on its default pins (with the index input) when no pin is given.
    When the default encoder is opened without pins
    Then it reads the resolution and speed 0
    And its index input reads 0 or 1
    When the default encoder is closed
    Then reading it fails with "notopen"

  Scenario: An encoder other than the default needs its pins
    Given the encoder is not the default encoder, which has default pins
    Then opening the encoder without pins fails with "usage"
    And opening the encoder with only its A pin fails with "usage"

  Scenario: The resolution is 2 to the maximum and the offset stays below it
    `res` is 2 to 65536, up to 4294967295 on the 32-bit TIM2; `offset` must stay below it.
    Then opening the encoder with resolution 1, one beyond its maximum or an offset not below the resolution fails with its reason
    When the encoder is opened at its maximum resolution with the offset one below it
    Then the encoder reads that offset as position and its maximum resolution

  Scenario: Malformed and out-of-range opens are refused, and the index input needs an open encoder with idx
    Then every malformed or out-of-range open of the encoder fails with its reason
    And reading the index input of the encoder fails with "notopen"
    When the encoder is opened without index input, the velocity "off" and filter 15
    Then reading the index input of the encoder opened without idx fails with "unsupported"

  Scenario: Every LPTIM capture mode opens and reads back the resolution
    Every LPTIM capture mode opens and reads back the resolution.
    Then the LPTIM encoder opens with every capture mode of the LPTIM position matrix, reads the resolution and closes

  Scenario: Malformed and unsupported LPTIM opens are refused
    Then every malformed, out-of-range or unsupported open of the LPTIM encoder fails with its reason
    And opening every missing LPTIM instance fails with "range"

  Scenario: One encoder is open at a time
    When the first encoder of the board file is opened
    Then opening the second encoder fails with "busy"

  Scenario: A timer that counts an encoder is busy for PWM and the ADC trigger
    A timer serves one group at a time: while it counts an encoder, PWM and the ADC trigger get `ERR busy`.
    Given the first encoder of the board file on a timer that can trigger the ADC
    When that encoder is opened
    Then opening PWM on its timer with channel 3 fails with "busy"
    And opening the ADC on its first input, triggered by that timer, fails with "busy"
