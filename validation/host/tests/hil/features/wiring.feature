Feature: Bench wiring
  Self-check of the bench wiring with GPIO commands only: run it first after wiring the board
  (`pytest tests/hil/test_wiring.py --port ... --wiring-set <set> [--with <tag> ...]`).

  With the AD3 outputs and pulls off, the MCU drives the key pin of each `jumpered` entry of an enabled option and
  reads the listed pins against the opposite MCU pull (continuity), reads the `pullups` pins against the MCU
  pull-down (external pull-ups on a live 3V3 rail), looks for the jumpers of offered options that are not enabled
  (fitted wiring the run does not know about), and checks that the pins of `tests.wiring.undriven` follow both MCU
  pulls (nothing else, such as an ST-LINK line through a solder bridge, drives them). Skipped with `--fake`: the
  fake firmware has no wiring.

  A listed pin follows its key pin when it reads 1 against its MCU pull-down while the key pin drives high, and 0
  against its MCU pull-up while the key pin drives low. The options, with their jumpered and pull-up pins, are in the
  wiring sets of the board file; the undriven pins are tests.wiring.undriven.

  Scenario: The jumpers of every enabled option are fitted
    Given the jumpered pin pairs of the option, skipping if it declares none
    Then the listed pin of every pair follows its key pin

  Scenario: The pull-ups of every enabled option reach a live 3V3 rail
    Given the pull-up pins of the option, skipping if it declares none
    Then every pull-up pin reads 1 against the MCU pull-down

  Scenario: No wiring of an option left out is fitted
    Given the jumpered pin pairs of the option, skipping if it declares none to look for
    Then no listed pin of a pair follows its key pin

  Scenario: Nothing else drives the undriven pins
    Given the pin, resolved, unless an enabled option loads it
    Then the pin reads 1 against the MCU pull-up and 0 against the MCU pull-down
