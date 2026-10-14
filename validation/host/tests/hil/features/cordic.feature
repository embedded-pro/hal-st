@family:stm32g474
Feature: CORDIC co-processor
  The CORDIC (`hal::SynchronousCordicStm`, `hal::CordicStm` on its interrupt, `hal::CordicStmDma` on DMA2 channels
  5 and 6) through `cordic.calc`: every function over its input domain against `cordic_ref` within the bound of the
  precision (4 iterations per cycle), q1.15 packing, accuracy that grows with the precision, the same words from the
  four variants, and the duration the firmware measures. No wiring. Functions, scales, precisions, the tolerance and
  the batch sizes are in tests.cordic of the board file.

  Scenario: Each function matches the reference within the bound of the precision
    Given arguments spread over the domain of the function at its scale
    When the firmware calculates them in q1.31 with the precision
    Then every result is within the bound of the precision from the reference

  Scenario: q1.15 packs two arguments and two results per word
    Given arguments spread over the domain of the function at its scale
    When the firmware calculates them in q1.15
    Then every q1.15 result is within the bound of the precision from the reference

  Scenario: A lower precision is less accurate
    Given arguments spread over the domain of the function at its scale
    When the firmware calculates them in q1.31 at every precision of the board file
    Then the largest error does not grow with the precision
    And the largest error at the lowest precision exceeds the one at the highest

  Scenario: The four variants calculate the same words
    When the firmware calculates the generated batch with each variant
    Then every variant answers the length and CRC of the polled batch

  Scenario: One calculation answers in every variant
    The CORDIC requests its interrupt when a result becomes ready, so a variant that misses the first one never
    answers, whatever the batch.
    When the firmware calculates the cosine of 0 alone with the variant
    Then the result is 1 within the bound of the default precision

  Scenario: DMA is faster than interrupts
    When the firmware calculates the generated batch with each variant
    Then the DMA variant took less time than the interrupt variant
    And every variant took more than 0 and at most the maximum duration of the board file

  Scenario: Malformed and out-of-range commands are refused
    Then the command line fails with the reason
