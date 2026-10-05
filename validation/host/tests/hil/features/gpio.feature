Feature: GPIO
  `hal::GpioPinStm`: levels, drive strengths, pulls, open drain, EXTI interrupts and timer-driven pulses.

  Wiring set `bundle1`: the pins of `tests.gpio.loop_pins`/`output_pins` are pins of other peripherals, used here as
  plain GPIO; `output_pins` is the user LED. An EXTI line serves one port at a time: `tests.gpio.exti_sharing` names
  two wired pins with the same index on different ports. The pins, the drives, the pulls, the pin limit, the interrupt
  matrix and the pulse parameters are in tests.gpio of the board file.

  @ad3
  Scenario: The pin drives the DIO with every drive strength
    Given the pin is wired to a DIO
    Then the pin, configured as an output with the drive strength, drives the DIO to every level it is set to

  @ad3
  Scenario: The output pins drive the DIO
    Given the pin is wired to a DIO
    Then the pin, configured as an output, drives the DIO to every level it is set to

  @ad3
  Scenario: The pin reads the level the DIO drives with every pull
    Given the pin is wired to a DIO
    Then the pin, configured as an input with the pull, reads every level the DIO drives, and the DIO is released

  @ad3
  Scenario: The pull sets the idle level
    Given the pin is wired to a DIO
    And the DIO is released
    When the pin is configured as an input with the pull
    Then the pin and the DIO read high if the pull is up and low otherwise

  @ad3
  Scenario: An open-drain pin pulls low and, released, follows the external level
    Given the pin is wired to a DIO
    And the DIO is released
    When the pin is configured as open drain
    And the pin is set to 0
    Then the DIO is pulled low
    When the pin is set to 1
    Then the released open-drain pin reads every level the DIO drives, and the DIO is released

  Scenario: Wrong configurations and commands on unopened pins are refused
    Given the first of the loop pins
    Then configuring the loop pin as open drain with pull "up" or with drive "fastest" fails with "usage"
    When the loop pin is configured as open drain with pull "none"
    And the loop pin is configured as an output with drive "high"
    Then the loop pin reads 0: an output starts low
    And setting the loop pin to 2 fails with "usage"
    When the loop pin is configured as an input
    Then pulsing the loop pin once for 1 ms fails with "usage": gpio.pulse needs an output
    When the loop pin is released
    Then gpio.get, gpio.set, gpio.count, gpio.irq and gpio.release of the loop pin fail with "notopen"

  Scenario: The group holds a limited number of pins
    RAM limits the GPIO group to `tests.gpio.limit` pins; reconfiguring a pin needs no new entry.
    Given the limit pins of the board file, more than the limit
    When as many limit pins as the limit are configured as inputs
    Then configuring the next limit pin as an input fails with "busy"
    When the first limit pin is configured as an input with pull "up"
    And the first limit pin is released
    Then the next limit pin can be configured as an input

  Scenario: Pins held by other groups are refused
    Given the first SPI instance of the board file is opened
    Then configuring its clk pin and then its cs pin as GPIO inputs fails with "busy"
    When the SPI instance is closed
    Then its cs pin can be configured as a GPIO input

  @ad3
  Scenario: The interrupt counts the edges of the pulses and stops counting when turned off
    Given the pin is wired to a DIO
    And the DIO is released
    And the pin is configured as an input with pull "down"
    And the interrupt on the edge is enabled with the handler
    And the edge count of the pin is cleared
    When the AD3 sends the pulses on the DIO at the frequency
    And the firmware waits 10 ms
    Then the edge count is the number of pulses, twice that for both edges
    When the interrupt is turned off
    And the AD3 sends 3 pulses on the DIO at the frequency
    Then the edge count is still the number of the first pulses, twice that for both edges

  Scenario: An EXTI line serves one port at a time
    While a pin counts edges on its EXTI line, the pin of another port with the same index is refused.
    Given the counting pin and the sharing pin of the EXTI sharing of the board file
    When the counting pin and the sharing pin are configured as inputs with pull "down"
    And the interrupt of the counting pin is enabled on the rising edge
    And the interrupt of the counting pin is enabled on both edges with the immediate handler
    Then enabling the interrupt of the sharing pin on the rising edge and turning it off fail with "unsupported"
    When the interrupt of the counting pin is turned off
    And the interrupt of the sharing pin is enabled on the falling edge
    Then enabling the interrupt of the counting pin on the rising edge fails with "unsupported"
    When the sharing pin is released
    Then the interrupt of the counting pin can be enabled on the rising edge

  @ad3
  Scenario: The owner of an EXTI line keeps counting
    The refused pin leaves the owner's interrupt alone, and edges on it are not counted.
    Given the counting pin and the sharing pin of the EXTI sharing of the board file
    And the counting pin and the sharing pin are wired to DIOs
    And both DIOs are released
    When the counting pin and the sharing pin are configured as inputs with pull "down"
    And the interrupt of the counting pin is enabled on the rising edge
    Then enabling the interrupt of the sharing pin on the rising edge fails
    When the edge count of the counting pin is cleared
    And the AD3 sends 7 pulses on the sharing DIO and then 5 pulses on the counting DIO at 1000 Hz
    And the firmware waits 10 ms
    Then the edge count of the counting pin is 5
    And the edge count of the sharing pin is 0

  @ad3
  Scenario: Timer-driven pulses toggle the pin at the period
    Given the pin is wired to a DIO
    And the pulse count, tolerance and jitter of the board file, the jitter 0.5 ms unless set
    And the pin is configured as an output
    And the pin is set to 0
    When the pin pulses the pulse count of times at the period while the logic analyzer records the DIO from just before its first rising edge
    Then the DIO toggles the pulse count of times
    And the intervals between the toggles average the period within the tolerance and none is off by more than the jitter, if there are any
