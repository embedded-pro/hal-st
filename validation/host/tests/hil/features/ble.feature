@pair
Feature: BLE central and peripheral
  Two boards linked over the air through the `ble` group of `hal_st.validation_ble_firmware`: one runs
  `hal::GapCentralSt` with `hal::GattClientSt`, the other `hal::GapPeripheralSt` with `hal::GattServerSt` and the Nordic
  UART service. Each board runs both roles in turn (module fixture: the `--port` board is the central, then the
  `--peer-port` board is), on NUCLEO-WB55RG and NUCLEO-WBA55CG in any combination. No wiring; the boards only have to
  be in radio range. Timeouts and the RSSI floor are tests.ble of the board files.

  Scenario: Both boards open their role and report the controller ready
    Then both boards report their role and address

  Scenario: The central finds an advertising peripheral
    Given the peripheral advertises
    When the central scans for the peripheral
    Then the central reports the peripheral with its name, public address and connectable advertising
    And the report carries a signal strength above the board files' minimum

  Scenario: A peripheral that stopped advertising is not reported
    Given the peripheral advertises
    And the peripheral stops advertising
    When the central scans for the peripheral
    Then the central reports nothing within the quiet window

  Scenario: The peripheral advertises under the name it is given
    When the peripheral advertises under the name hal-st-renamed
    And the central scans for the peripheral
    Then the central reports the peripheral under the name hal-st-renamed

  Scenario: The central connects to the advertising peripheral
    Given the peripheral advertises
    When the central connects to the peripheral
    Then both boards report the connection with the address of the other
    And both boards report the same link parameters
    And both boards report the link in their link state

  Scenario: The central ends the link
    Given the boards are connected
    When the central disconnects
    Then the central reports the local host as the reason
    And the peripheral reports the remote user as the reason
    And both boards are idle

  Scenario: The peripheral ends the link
    Given the boards are connected
    When the peripheral disconnects
    Then the peripheral reports the local host as the reason
    And the central reports the remote user as the reason
    And both boards are idle

  @slow
  Scenario: The boards connect and disconnect again and again
    When the boards connect and disconnect for the reconnect cycles of the board files
    Then every cycle ended with both boards idle

  Scenario: Connecting to an address nobody advertises times out
    When the central connects to an address nobody advertises with a timeout of 1000 ms
    Then the central reports that the connection failed because of the timeout
    And the central is idle

  Scenario: A connection attempt can be cancelled
    When the central connects to an address nobody advertises
    And the central cancels the connection attempt
    Then the central reports that the connection failed because it was cancelled
    And the central is idle

  Scenario: Both boards agree on the MTU
    Given the boards are connected
    When the central exchanges the MTU
    Then both boards report the same MTU of at most 251 bytes
    And both link states hold that MTU

  Scenario: The Nordic UART service opens on both boards
    Given the boards are connected
    When the central discovers the Nordic UART service
    Then both boards report the service open

  Scenario: Data crosses the link in both directions
    Given the boards are connected with the Nordic UART service open
    When the central sends the payload
    Then the peripheral receives the same payload
    When the peripheral sends the payload
    Then the central receives the same payload

  Scenario: A payload beyond what the link carries is refused
    Given the boards are connected with the Nordic UART service open
    Then a payload one byte larger than the service allows is refused with range on both boards

  Scenario: Sending without an open service is refused
    Then both boards refuse to send with notopen
    When the boards connect
    Then both boards refuse to send with notopen

  Scenario: The central changes the connection parameters
    Given the boards are connected
    When the central requests a connection interval of 40 units
    Then both boards report the new connection parameters
    And both link states hold them

  Scenario: The central keeps its own parameters when the peripheral asks for others
    A peripheral's L2CAP update request is answered with the parameters the central used last.
    Given the boards are connected
    And the central has requested a connection interval of 24 units
    When the peripheral requests a connection interval of 80 units
    Then both link states still hold the central's interval after the quiet window

  Scenario: The central changes the PHY
    Given the boards are connected
    When the central requests the PHY for both directions
    Then both boards report the PHY
    And both link states hold it

  Scenario: The central requests the largest data length
    Given the boards are connected with the Nordic UART service open
    When the central requests the largest data length
    Then both boards report the same data length
    And a payload as large as the link carries still arrives intact

  Scenario: Pairing bonds both boards
    Given the boards are connected with security level 2 on both
    When the central pairs
    Then both boards report the pairing complete with the same strength
    And each board holds one bond

  Scenario: A bond survives a reconnection
    Given the boards are paired
    When the boards disconnect and connect again
    Then each board still holds one bond
    And data still crosses the link

  Scenario: Removing the bonds empties both boards
    Given the boards are paired
    When both boards remove their bonds
    Then each board holds no bond

  Scenario: A peripheral that refuses pairing fails it
    Given the boards are connected with security level 2 on both
    And the peripheral refuses pairing
    When the central pairs
    Then the central reports the pairing failed
    And each board holds no bond

  Scenario: Commands of the other role are refused
    Then the peripheral refuses the central commands with unsupported
    And the central refuses the peripheral commands with unsupported

  Scenario: A second open is refused
    Then both boards refuse to open a role again with busy

  Scenario: Malformed and out-of-range commands are refused
    Then every malformed or out-of-range ble command line fails with its reason
