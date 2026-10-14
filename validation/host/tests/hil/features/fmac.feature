@family:stm32g474
Feature: Filter math accelerator
  The FMAC (`hal::SynchronousFmacStm`, `hal::FmacStm` on its interrupt, `hal::FmacStmDma` on DMA2 channels 7 and 8)
  through `fmac.load`, `fmac.fir` and `fmac.iir`: the calibration vectors of `fmac_ref` that tell the readings of the
  arithmetic apart (coefficient order, truncation, accumulator width and wrap, gain, clipping, feedback sign, history
  order), FIR and IIR filters bit for bit against the model on every variant, the same output from every variant,
  the stores kept between commands, and the duration. No wiring. Filter sizes, gains and the generated data are in
  tests.fmac of the board file.

  Scenario: The calibration vector matches the model
    Given the stores loaded with the vector
    When the firmware filters the inputs of the vector
    Then the outputs and the saturation flag are the model's

  Scenario: A FIR matches the model bit for bit
    Given generated coefficients for the taps in the b store
    When the firmware runs the FIR on the generated input with the gain and the variant
    Then the CRC and the saturation flag of the output are the model's

  Scenario: An IIR matches the model bit for bit
    Given generated coefficients for the IIR in the b and a stores
    When the firmware runs the IIR on the generated input with the variant
    Then the CRC and the saturation flag of the output are the model's

  Scenario: Every variant gives the same output and DMA is faster than interrupts
    Given generated coefficients for 31 taps in the b store
    When the firmware runs the 31-tap FIR on the generated input with every variant
    Then every variant answers the CRC of the polled run
    And the DMA run took less time than the interrupt run
    And every run took more than 0 and at most the maximum duration of the board file

  Scenario: The stores keep their values between commands
    Given generated coefficients for 8 taps in the b store
    Then two runs of the 8-tap FIR give the same output
    When the b store is cleared
    Then the 8-tap FIR gives zeros

  Scenario: Malformed and out-of-range commands are refused
    Then the command line fails with the reason
