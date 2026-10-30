@family:stm32g474
Feature: CAN
  `hal::FdCanStm` through EMIL's `can` group (classic frames on FDCAN 1-3 of the STM32G474) over the link `--can-mode`
  selects:

  - `loopback`: the FDCAN external loopback mode (`loopback=1`), in which the controller receives its own frames and
    still drives them onto the TX pin; needs no wiring;
  - `bus`: FDCAN1 through a 3.3 V transceiver to a CANable (`--can-peer`); every frame goes from the firmware to the
    CANable and back.

  The bus scenarios need the CANable whatever `--can-mode` is: a missing acknowledge (CANable listen-only), receive
  errors and bus off (CANable at a wrong bit rate), the bus-off recovery and the TX pin in loopback mode. The fake bus
  of `--fake` does not model bit errors.

  The controller is the CAN instance of tests.can. It is open when the firmware has opened it on its RX and TX pins
  with the options a step names, and open on the link when it is also in loopback mode on the loopback link. The
  CANable runs at a bit rate when it has been configured at that bit rate rounded to a whole bit/s (listen-only if a
  step says so) and emptied; the scenario skips when the CANable cannot run at it. The bit rate of a timing (tseg1,
  tseg2, sjw, brp) is the FDCAN kernel clock of the board file (the 24 MHz HSE) / (brp * (1 + tseg1 + tseg2)).

  The frame of an id type and a data length has the id of that type in tests.can.ids at the data length modulo their
  number, and data bytes counting up from 16 times the data length. A frame crosses the link when, sent by the
  firmware with can.send, it arrives back at the firmware within 1 s with its id, id type and data; on the bus the
  CANable first receives it within 1 s with its data and sends it back. A frame comes back to a controller in
  loopback mode when, sent with can.send, it arrives at the firmware within 1 s with its id, id type and data. A
  frame is sent on the link by the firmware in loopback and by the CANable on the bus. The firmware sends a frame in
  the background with a can.send command that has up to 3 s to answer: the host acts while it runs and then waits
  for its answer.

  The instances, bit rates, timings, filter cases and recovery settings are in tests.can of the board file, the bus
  bit rate and the wrong bit rate in tests.can.bus.

  Scenario: A frame of the id type and data length crosses the link at the bit rate
    Given the CANable runs at the bit rate if the link is the bus
    And the controller is open on the link at the bit rate
    Then the frame of the id type and data length crosses the link

  Scenario: Frames cross the link at the bit rate of an explicit timing
    On the bus, the CANable runs at the bit rate of the timing: frames only pass at that bit rate.
    Given the CANable runs at the bit rate of the timing if the link is the bus
    And the controller is open on the link with the timing
    Then the standard and then the extended frame of 8 bytes cross the link

  Scenario: The acceptance filter passes the frames it matches and drops the others
    Standard and extended filters, with and without `match` (frames of the other id type pass unfiltered when 0). On
    the bus the CANable sends the frames.
    Given the CANable runs at the bus bit rate if the link is the bus
    And the controller is open on the link at the bus bit rate with the filter of the case
    Then each accepted frame of the case, sent on the link with data "01", arrives at the firmware within 1 s with its id type and that data
    When each rejected frame of the case is sent on the link with data "02"
    Then after 100 ms no other frame has arrived at the firmware

  Scenario: Every FDCAN instance carries frames on its pins
    Each instance in loopback mode on pins of its own: its interrupt line and the alternate function of its pins.
    Given the controller is the instance
    And the controller is open in loopback mode at the bus bit rate
    Then the standard and then the extended frame of 8 bytes come back to the controller

  Scenario: A frame cannot be sent before the controller is open
    Then sending a frame with id 0x100 and data "00" on the controller fails with "notopen"

  Scenario: Invalid options are refused with their reason
    Then opening the controller on its pins with the options fails with the reason

  Scenario: A malformed timing or filter is refused with usage
    A timing takes four numbers, a filter three or four with the id type and match 0 or 1.
    Then opening the controller on its pins with the raw option fails with "usage"

  Scenario: A frame the controller cannot send is refused with its reason
    Ids beyond their type, more than 8 data bytes and malformed arguments.
    Given the controller is open in loopback mode at the bus bit rate
    Then sending the arguments on the controller fails with the reason

  Scenario: FDCAN1 opens on its default pins, the other instances need theirs
    FDCAN1 opens on `can1rx`/`can1tx` without pins, which it holds while it is open; a single pin is not enough, and
    the other instances have no default pins.
    Then FDCAN 1 opens in loopback mode without pins
    And configuring can1rx or can1tx as a GPIO input fails with "busy"
    And FDCAN 1 closes again
    And opening FDCAN 1 on the RX pin of the controller only fails with "usage"
    And opening FDCAN 2 without pins fails with "usage"

  Scenario: One controller is open at a time
    Given the controller is open in loopback mode at the bus bit rate
    Then opening the second instance of tests.can.instances in loopback mode on its pins fails with "busy"
    And opening the controller again fails with "busy"

  Scenario: A frame nobody acknowledges fails with ackError
    A listen-only CANable sees the frame but does not acknowledge it: the send fails with `ackError`.
    Given the bus is not the fake bus
    And the CANable runs listen-only at the bus bit rate
    And the controller is open at the bus bit rate
    When the firmware sends the standard frame of 4 bytes in the background while the CANable waits up to 1 s for it
    Then the CANable has received the frame with its data
    And can.send has answered "failed" or "timeout"
    And the controller has reported "ackError"

  Scenario: Frames at a wrong bit rate are reported as receive errors
    Frames sent by the CANable at a wrong bit rate are reported as receive errors (`EVT can error=`).
    Given the bus is not the fake bus
    And the CANable's bit rate can be set
    And the CANable runs at the wrong bit rate
    And the controller is open at the bus bit rate
    When the CANable sends a standard frame with id 0x0 and 8 zero bytes
    Then after 150 ms the controller has reported at least one error, all of them known CAN error names

  Scenario: Bus off fails the pending frame and ends by itself only with the automatic recovery
    The CANable at a wrong bit rate destroys every attempt of a frame with error flags, so the controller's transmit
    error counter reaches bus off; the frame in flight fails and is not sent after the recovery. Back at the right bit
    rate, `recover=1` has returned to the bus by itself and the next frame leaves; with `recover=0` the controller
    stays off.
    Given the bus is not the fake bus
    And the CANable's bit rate can be set
    And the CANable runs at the wrong bit rate
    And the controller is open at the bus bit rate with the recovery setting
    When the firmware sends a standard frame with id 0x0 and 8 zero bytes in the background while the controller reports busOff within 2 s
    Then can.send of that frame has answered "failed"
    When the CANable switches to the bus bit rate
    Then after 200 ms the CANable has received no frame with the id and id type of the failed frame
    When the firmware sends the standard frame of 4 bytes in the background while the CANable waits up to 1 s for it
    Then the CANable has received the frame and can.send has answered OK if the recovery setting is on, and neither if it is off

  Scenario: A frame sent in loopback mode still leaves on the TX pin
    The external loopback mode drives the TX pin: the CANable receives the frame the controller receives itself.
    Given the CANable runs at the bus bit rate
    And the controller is open in loopback mode at the bus bit rate
    When the firmware sends the standard frame of 4 bytes in the background while the CANable waits up to 1 s for it
    Then the CANable has received the frame with its data
    And can.send has answered OK
    And the frame has come back to the controller
