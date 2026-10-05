Feature: Send-only UART
  `hal::SynchronousUartStmSendOnly`, `uart.open ... sendonly=1` against the AD3 protocol UART.

  Wiring set `bundle1`: TX, RTS and CTS of every `tests.uart_sendonly.instances` entry on DIOs; the AD3 UART also
  needs a transmit DIO, the instance's `rx` pin, which the firmware leaves alone. STM32WB55 builds LPUART1 through the
  `SyncLpUart` constructors; STM32WBA55 has none, so its LPUART answers `ERR unsupported` and USART2 is the instance.

  `SendData` returns once the last byte has left the data register (TXE), not the wire (TC): `uart.send` answers while
  the last frame is still shifting out, and a `uart.close` that follows at once may cut that frame
  (`test_close_right_after_send`).

  The AD3 listens at a baud rate when its protocol UART runs at that rate, receiving on the DIO of the instance's TX
  pin and transmitting on the DIO of its RX pin, and its receiver has been emptied. The instance is opened send-only
  when uart.open opens it on its TX pin with sendonly=1. A payload sent by the firmware arrives at the AD3 when, sent
  with uart.send, the AD3 reads it back within its transfer time and 1 s. The instances, the baud rates, the payloads,
  the cut baud rate and payload size and the instances without send-only constructors are in tests.uart_sendonly of
  the board file.

  @ad3
  Scenario: Every payload leaves at every baud rate
    Given the AD3 listens at the baud rate
    And the instance is opened send-only at the baud rate
    Then each of the payloads sent by the firmware at the baud rate arrives unchanged at the AD3
    And the AD3 saw no parity errors

  @ad3
  Scenario: The send-only driver receives nothing
    `ReceiveData` of the send-only driver fails at once: bytes on the `rx` line never arrive.
    Given the AD3 listens at 115200 baud
    And the instance is opened send-only with its RX pin
    When the AD3 writes 55aa
    Then uart.recv of 2 bytes within 100 ms returns nothing

  @ad3
  Scenario: flow=rts drives RTS asserted and does not hold transmission
    `flow=rts` muxes RTS and enables RTS flow control: the UART drives the RTS pin low (receiver ready) against both
    AD3 pulls, and transmission is not held. CTSE cannot be observed: the send-only driver never muxes a CTS pin.
    Given the RTS pin of the instance is wired to a DIO
    And the AD3 listens at 115200 baud
    And the instance is opened send-only at 115200 baud with flow=rts on its RTS pin
    Then the RTS DIO reads 0 with the AD3 pulling it up and with the AD3 pulling it down, the pulls turned off afterwards
    And the last of the payloads sent by the firmware at 115200 baud arrives unchanged at the AD3, flow=rts not blocking it

  @ad3
  Scenario: A close right after a send may cut only the last frame
    `uart.close` right after `uart.send`: every frame but the last arrives intact; the last one may be cut (the
    driver waits for TXE, not TC).
    Given the AD3 listens at the cut baud rate
    And the AD3 pulls its receive DIO up
    And the instance is opened send-only at the cut baud rate
    When the instance sends a payload of the cut payload size, counting up from 30 masked to 7 bits, and is closed right after uart.send answers
    Then the AD3 reads every byte of the payload but the last unchanged, and no more bytes than the payload has

  Scenario: The RX pin is optional
    When the instance is opened send-only
    Then uart.recv returns nothing
    When the instance is closed
    Then the instance opens send-only with its RX pin

  Scenario: Options the send-only driver lacks are refused
    `sendonly` excludes the other driver selections and the options `SynchronousUartStmSendOnly` lacks.
    Then opening the instance send-only with each of the option sets it lacks fails with the reason of the set
    And opening the instance send-only on its RX pin without its TX pin fails with "usage"

  Scenario: An LPUART without send-only constructors is refused
    LPUARTs without send-only constructors (STM32WBA).
    Then opening the instance send-only fails with "unsupported"

  Scenario: Without pins the default pins apply
    Without pins the defaults of the instance apply (LPUART1), as for the other drivers.
    Given the instance has default pins
    Then the instance opens send-only without pins
