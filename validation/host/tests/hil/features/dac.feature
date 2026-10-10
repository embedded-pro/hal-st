@family:stm32g474
Feature: DAC output
  The DAC outputs of the board through the `dac` group (`hal::DigitalToAnalogPinImplStm` on `hal::DacStm`, buffer on, the
  pin only): DAC1 OUT1 on PA4 and DAC2 OUT1 on PA6 of the STM32G474. The AD3 scope reads the output voltage with
  `--with dac` (wiring set `bundle3`: scope 1 on PA4, scope 2 on PA6); the argument checks, the clamping and the pin
  ownership need no AD3. The pins, the codes and the tolerances are in tests.dac of the board file.

  Scenario: The output follows the code
    Given the scope is wired to the DAC output
    When the output is opened and written with the code
    Then the scope measures the voltage of the code within the tolerance

  Scenario: The full range reaches the rails of the buffer
    Given the scope is wired to the DAC output
    When the output is opened and written with the lowest code
    Then the scope measures at most the low limit
    When the output is written with the highest code
    Then the scope measures at least the high limit

  Scenario: Both outputs run at once at their own codes
    Given the scope is wired to both DAC outputs
    When both outputs are opened and written with different codes
    Then each scope measures the voltage of its own code within the tolerance

  Scenario: A code above the resolution is clamped
    When the output is opened and written with a code of 65535
    Then the firmware reports the highest 12-bit code

  Scenario: Invalid arguments are refused with their reason
    Then opening a pin without a DAC output fails with "pin"
    And writing a closed output fails with "notopen"
    And closing a closed output fails with "notopen"
    And opening an output twice fails with "busy"
    And writing a code beyond 16 bits fails with "range"
    And a command with a missing argument fails with "usage"

  Scenario: The pin returns to the pool on close
    When the output is opened and closed
    Then the pin can be configured as a GPIO input
    And the output can be opened again
