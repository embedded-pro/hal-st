@resets_board
Feature: Watchdog
  Watchdog (`hal::WatchDogStm`, the window watchdog): early warnings, feeding, resets and the warning period.

  A started watchdog cannot be stopped, so every test resets the board afterwards. The warning period is
  63 * 4096 * prescaler / PCLK1 for the smallest prescaler whose period is at least the timeout
  (`expect.wwdg_warning_period`). `tests.watchdog.pin` is a bundle1 pin: the `pin=` toggle output is on its DIO, so the
  logic analyzer measures the warning period. The index, the pin, the timeouts, the observed periods and the
  tolerance are in tests.watchdog of the board file.

  @slow
  Scenario: Fed, the watchdog warns once per period; unfed, it resets the board
    `feed=auto` keeps the board alive with one warning per period; with `feed=manual` the warning that is not
    answered is followed by a reset reported as `reset=wwdg`. The reset comes one counter tick after the warning,
    which at the shortest timeouts is too short to send the `EVT wdt` line: the line is only checked where the tick
    leaves time for it (`expect.wwdg_warning_outruns_reset`).
    When the watchdog starts with the timeout and the feed mode
    Then if the feed is manual, the board warns once where the warning line outruns the reset, then resets with reason "wwdg", which info reports too
    And if the feed is not manual, the watchdog warns about once per warning period over the observation window without a reset, and the board still answers ping

  @ad3 @slow
  Scenario: The pin toggles once per warning period
    The `pin=` output toggles on every early warning: the toggle interval is the warning period.
    Given the watchdog pin is wired to a DIO
    When the logic analyzer arms on either edge of the pin for the observed warning periods
    And the watchdog starts with the timeout, automatic feed and the pin
    Then the pin toggles at least once per observed warning period, at the warning period within the period tolerance
    And the board did not reset although it was fed

  Scenario: Manual feeding keeps the board alive until it stops
    `wdt.feed` faster than the warning period keeps the board alive; when it stops, the board resets.
    When the watchdog starts with the manual timeout and manual feed
    And the host feeds it every quarter warning period for the feed time
    Then the board did not reset while it was fed
    And the board resets with reason "wwdg" within three warning periods and the boot timeout

  Scenario: Only one watchdog starts
    A started watchdog cannot be stopped, so a second start is refused; its pin stays claimed.
    When the watchdog starts with the manual timeout and the pin
    Then starting it again with the manual timeout fails with "busy"
    And configuring the pin as an input fails with "busy"

  Scenario: The timeout is refused beyond the reach of the WWDG
    `timeout` is 1-30000 ms, and at most what the WWDG reaches at PCLK1 (`ERR range` beyond).
    Then starting the watchdog with the timeout fails with "range" exactly when the timeout is past the largest one or out of reach of the WWDG at PCLK1

  Scenario: Invalid start arguments are refused
    Then every invalid start is refused with its reason
    And the malformed command lines are refused with their reason
