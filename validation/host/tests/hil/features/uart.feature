Feature: UART
  `hal::UartStm`, `UartStmDma`, `UartStmDuplexDma`, `SynchronousUartStm` against the AD3 protocol UART.

  Wiring set `bundle1`: TX, RX, RTS and CTS of every `tests.uart.instances` entry on DIOs; the logic analyzer also
  records the firmware TX line to decode the frames and measure the bit rate. USART1 is the terminal, so the
  instances under test are LPUART1 (both boards) and USART2 (NUCLEO-WBA55CG). A receive overrun drops bytes
  (`UartStm` clears ORE and goes on), so a streaming failure names it.

  An instance is open against the AD3 when the firmware has opened it on its TX and RX pins with the baud rate, the
  parity (none unless a step names it) and the driver of the variant (interrupt unless a step names it, see
  tests.uart.variants), the AD3 protocol UART runs with the same settings, receiving on the DIO of the TX pin and
  transmitting on the DIO of the RX pin, and both receivers have been emptied. A payload goes from the firmware to
  the AD3 when, sent with uart.send, it arrives unchanged at the AD3 without parity errors; it goes from the AD3 to
  the firmware when, written by the AD3, uart.recv returns it. The instances, the variants, the payloads, the large
  payload sizes, the stream rounds and size, the flow baud rate, the stall payload size, the reopen settings, the
  open baud rates and the bit rate tolerance are in tests.uart of the board file.

  @ad3
  Scenario: Payloads cross both ways and the TX line runs at the baud rate
    Given the instance is open against the AD3 at the baud rate with the parity and the variant
    Then each of the payloads goes from the firmware to the AD3 and then from the AD3 to the firmware
    And 16 bytes 55 sent by the firmware decode from its TX line without parity or framing errors, at the baud rate within the bit rate tolerance, unless the logic analyzer cannot record them at 16 samples per bit

  @ad3
  Scenario: The largest payloads cross both ways
    Given the instance is open against the AD3 at the baud rate with the variant
    Then a payload counting up from 00, as long as one uart.send command line can carry but at most the large payload size and 256 bytes, goes from the firmware to the AD3
    And a payload of the large payload size to the firmware, counting in steps of 7, goes from the AD3 to the firmware

  @ad3
  Scenario: Both directions stream at once without loss or reordering
    Both directions at once, round after round: nothing may be lost or reordered.
    Given the instance is open against the AD3 at the baud rate with the variant
    Then in each of the stream rounds, while the firmware sends a block of the stream size the AD3 sends another one, uart.send succeeds and each block arrives unchanged at the other end

  @ad3
  Scenario: CTS holds the transmitter and RTS is asserted while the firmware can receive
    CTS deasserted (high) holds the firmware's transmitter; RTS is asserted (low) while it can receive. A
    stalled send is released before anything else happens, so `sync=1` (which blocks the firmware) recovers.
    Given the instance is open against the AD3 at the flow baud rate with the variant and the flow control on the RTS and CTS pins it uses, CTS driven high first if it uses it
    Then RTS reads low and "0123456789abcdef" goes from the AD3 to the firmware, if the flow control uses RTS
    And "0123456789abcdef" goes from the firmware to the AD3, if the flow control does not use CTS
    And "0123456789abcdef" sent by the firmware does not reach the AD3 within 0.2 s, reaches it within 1 s once the AD3 drives CTS low, and uart.send succeeds, if the flow control uses CTS

  @ad3
  Scenario: uart.send answers ERR timeout while CTS is held, and the stalled data leaves once it is released
    PROTOCOL: `uart.send` answers `ERR timeout` if the driver never completes; releasing CTS lets the stalled
    data out and the instance keeps working.
    Then a send of the stall payload, by the instance open against the AD3 at the flow baud rate with the variant and RTS/CTS flow control while the AD3 drives CTS high, answers "timeout", and afterwards the AD3 drives CTS low
    And the stalled payload reaches the AD3 within 1 s
    And "a55a" goes from the firmware to the AD3 at the flow baud rate

  @ad3
  Scenario: An instance closed during a stalled send opens again and works
    Closing during a stalled send and opening again gives a working instance (each open rebuilds it).
    Then a send of the stall payload, by the instance open against the AD3 at the flow baud rate with the variant and RTS/CTS flow control while the AD3 drives CTS high, answers "timeout" and the instance is closed, and afterwards the AD3 drives CTS low
    When the host waits 0.05 s
    And the instance is opened against the AD3 at the flow baud rate with the variant
    Then the board answers ping
    And the last of the payloads goes from the firmware to the AD3 and then from the AD3 to the firmware at the flow baud rate

  @ad3
  Scenario: The instance reopens with other settings
    Then with each of the reopen settings in turn, the instance is opened against the AD3 at its baud rate and parity, "5aa5" goes from the firmware to the AD3, the instance is closed and 10 ms pass

  Scenario: uart.open accepts exactly the baud rates the baud-rate register fits
    `uart.open` succeeds exactly where the divider fits the baud-rate register at the kernel clock (and below
    the HAL limit of the family).
    Given whether the baud rate fits the baud-rate register at the kernel clock of the instance and the baud rate limit of the family
    Then opening the instance on its TX and RX pins at the baud rate succeeds and the instance closes if the baud rate fits, and fails with "range" otherwise

  Scenario: Invalid options are refused with their reason
    Then opening the instance with each of the invalid option sets fails with the reason of the set
    And opening the instance with stop=2 fails with "usage"

  Scenario: Only an instance with default pins opens without pins
    Only LPUART1 has default pins; every other instance needs `tx` and `rx`.
    Then the instance opens without pins and closes, if it has default pins
    And opening the instance without pins fails with "usage", if it has no default pins

  Scenario: Only one UART is open at a time
    Given the first two of the instances, unless the board has only one
    When the first of them is opened on its TX and RX pins
    Then opening the second on its TX and RX pins fails with "busy"

  Scenario: uart.send needs an open instance, and malformed or out-of-range send and receive commands are refused
    Then sending 55 on the instance fails with "notopen"
    When the instance is opened on its TX and RX pins
    Then each malformed or out-of-range uart.send and uart.recv command line of the instance fails with its reason

  @ad3 @resets_board
  Scenario: swap=1 exchanges the functions of the two pins
    `swap=1` exchanges the functions of the two pins: the firmware transmits on its `rx` pin.
    Given the instance is open with the variant and swap=1 at 115200 baud against the AD3 receiving on the DIO of its RX pin and transmitting on the DIO of its TX pin
    Then the last of the payloads goes from the firmware to the AD3 and then from the AD3 to the firmware at 115200 baud

  @ad3 @resets_board
  Scenario: swap=1 does not survive a close
    After a `swap=1` session is closed, a plain open transmits on its `tx` pin again.
    When the instance is opened on its TX and RX pins at 115200 baud with swap=1 and closed
    And the instance is opened on its TX and RX pins at 115200 baud
    And the AD3 UART runs at 115200 baud, transmitting on the DIO of the RX pin and receiving on the DIO of the TX pin, and is emptied
    Then the last of the payloads goes from the firmware to the AD3 at 115200 baud
