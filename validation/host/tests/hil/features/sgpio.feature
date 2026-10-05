Feature: Synchronous GPIO
  Synchronous GPIO (`hal::SynchronousOutputPinStm`, `hal::SmallPeripheralPinStm`, `hal::MultiGpioPinStm` with
  `hal::MultiPeripheralPinStm`) through the `sgpio` group.

  Wiring set `bundle1`: the pins of `tests.sgpio` on DIOs. The output pins are read with the AD3 static I/O. An
  open-drain output has no pull (`SynchronousOutputPinStm` takes none): on `out_pins` only its low level and its latch
  are asserted, on `float_pins` (no board load) the AD3 pulls show that a high level releases the line. The
  alternate-function cases mux a timer channel to the pins while the `tpwm` group runs that channel with no pin of its
  own (`-`): the PWM appears on every muxed pin exactly when the mux works. The pins, the speeds, the
  alternate-function pin, the set, the timer of `tpwm`, the recording periods and the tolerances are in tests.sgpio of
  the board file.

  @ad3
  Scenario: A push-pull output drives the DIO with every speed and latches the level
    Given the pin is wired to a DIO
    And the DIO is released
    Then the pin, set as an output with the speed to every level in turn, drives the DIO to it and latches it

  @ad3
  Scenario: An open-drain output pulls low and keeps open drain on a later out
    Given the pin is wired to a DIO
    And the DIO is released
    When the pin is set as an open-drain output to 0
    Then the open drain pulls the DIO low
    And the latch of the pin reads 0
    When the pin is set to 1
    Then the latch of the pin reads 1: a later out keeps open drain and sets the latch
    When the pin is set to 0
    Then the DIO reads 0

  @ad3
  Scenario: Open drain releases the line, push-pull drives it and the released pin floats
    Open drain releases the line at 1 and holds it low at 0; a changed `od` rebuilds the pin as push-pull, which
    drives the high level against both pulls; `sgpio.release` leaves an input without pull (`float_pins` carry no
    board load, so the AD3 pulls decide its level).
    Given the pin is wired to a DIO
    And the DIO is released
    When the pin is set as an open-drain output to 1
    Then the line follows the AD3 pulls: open drain at 1 releases it
    When the pin is set as an open-drain output to 0
    Then the line does not follow the AD3 pulls: open drain at 0 holds it low
    When the pin is set as a push-pull output to 1
    Then the line does not follow the AD3 pulls: push-pull high overrides the AD3 pull-down
    And the DIO reads high after open drain
    When the pin is released
    Then the line follows the AD3 pulls: the released pin is an input without pull

  @ad3
  Scenario: A timer channel muxed to the alternate-function pin drives the PWM there
    Given the alternate-function pin is wired to a DIO
    And the timer of tpwm runs its first channel with no pin
    Then muxing the alternate-function pin to the timer channel reports its alternate function number
    And the PWM appears on the DIO
    When the alternate-function pin is released
    Then the line follows the AD3 pulls: SmallPeripheralPinStm leaves an input without pull

  @ad3
  Scenario: A timer channel muxed to the set drives the PWM on every pin of it
    Given the pins of the set are wired to DIOs
    And the timer of tpwm runs its first channel with no pin
    When the pins of the set are muxed to the timer channel
    Then the PWM appears on every DIO of the set
    When the last pin of the set is released
    And the AD3 pulls the DIOs of the set down
    Then no DIO of the set shows an edge for the recording periods

  Scenario: A raw alternate function number is muxed and reported
    `af=<n>` muxes a raw alternate function and reports it.
    Then muxing the alternate-function pin to its raw alternate function number reports that number
    And the alternate-function pin is released

  Scenario: The group has four outputs, four alternate-function pins and one set
    Four outputs, four alternate-function pins and one set at a time; a pin serves one use.
    When the first four limit pins are set as outputs to 0
    Then setting the fifth limit pin as an output to 0 fails with "busy"
    And muxing alternate function 0 to the first limit pin fails with "busy"
    When the first four limit pins are released
    And alternate function 0 is muxed to the first four limit pins
    Then muxing alternate function 0 to the fifth limit pin fails with "busy"
    And setting the first limit pin as an output to 0 fails with "busy"
    When the first four limit pins are released
    And the pins of the set are muxed to the timer channel
    Then muxing the first pin of the set alone to the timer channel fails with "busy"
    And setting the first pin of the set as an output to 0 fails with "busy"
    When the first pin of the set is released
    Then every pin of the set can be set as an output to 0

  Scenario: A pin serves either the gpio or the sgpio group
    Given the first output pin is configured as a GPIO input
    Then setting the first output pin as an sgpio output to 1 fails with "busy"
    When the GPIO of the first output pin is released
    And the first output pin is set as an sgpio output to 0
    Then configuring the first output pin as a GPIO input fails with "busy"

  Scenario: Malformed, out-of-range, reserved and foreign commands are refused
    Then every malformed, out-of-range, reserved or foreign sgpio command line fails with its reason
