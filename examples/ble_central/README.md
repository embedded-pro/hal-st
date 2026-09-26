# ble_central

A BLE central driven entirely from a terminal on the ST-Link virtual COM port. It scans,
connects, runs the GAP security procedures on demand, discovers the Nordic UART service on the
peer and pipes text over it.

Its counterpart is [`ble_peripheral`](../ble_peripheral/README.md); flashing one board with each
gives a link that can be driven from both ends.

## Targets

| Board          | Preset       | Status                                                           |
|----------------|--------------|------------------------------------------------------------------|
| NUCLEO-WB55RG  | `stm32wb55`  | Built by default                                                 |
| NUCLEO-WBA55CG | `stm32wba55` | Built by default; not functional yet — see [STM32WBA](#stm32wba) |

```bash
cmake --preset stm32wb55
cmake --build --preset stm32wb55-RelWithDebInfo --target examples_st.ble_central
```

On WB55 the wireless coprocessor has to be running the full BLE stack that matches the middleware
in tree, `stm32wb5x_BLE_Stack_full_fw.bin` from STM32CubeWB v1.24.0 in
[`hal_st/middlewares/STM32_WPAN/STM32CubeWB/binaries`](../../hal_st/middlewares/STM32_WPAN/STM32CubeWB/binaries/README.txt).
It installs only over FUS V2.x, so update the FUS first if needed; flash both with STM32CubeProgrammer
before running this example.

## Terminal

The ST-Link virtual COM port carries both the trace output and the terminal, at 115200 8N1 on
USART1 — PB6/PB7 on WB55, PB12/PA8 on WBA55. `help` lists everything; each command has a short
alias.

### Discovery and the link

| Command                                                | Alias | Description                                                                                        |
|--------------------------------------------------------|-------|----------------------------------------------------------------------------------------------------|
| `scan`                                                 | `sc`  | Start active device discovery; each report is traced with address, type, event type, RSSI and name |
| `stopscan`                                             | `ss`  | Stop device discovery                                                                              |
| `connect <aa:bb:cc:dd:ee:ff> <type>`                   | `c`   | Connect to a peer; type 0 public, 1 random                                                         |
| `cancel`                                               | `ca`  | Cancel an outstanding connection attempt                                                           |
| `disconnect`                                           | `dc`  | Disconnect the current link                                                                        |
| `connectionparameters <min> <max> <latency> <timeout>` | `cp`  | Update the connection parameters; intervals in units of 1.25 ms, timeout in units of 10 ms         |
| `phy <tx> <rx>`                                        | `ph`  | Request a PHY; 0 for 1M, 1 for 2M, 2 for coded                                                     |
| `datalength`                                           | `dl`  | Request the maximum data length for LE 2M                                                          |
| `mtu`                                                  | `m`   | Exchange the ATT MTU                                                                               |
| `state`                                                | `st`  | Show the link state, the pipe state and the bond count                                             |

### Security

| Command                        | Alias | Description                                                      |
|--------------------------------|-------|------------------------------------------------------------------|
| `security <level>`             | `sec` | Security mode 1, level 1 to 4                                    |
| `iocapabilities <caps>`        | `io`  | 0 display, 1 displayYesNo, 2 keyboard, 3 none, 4 keyboardDisplay |
| `secureconnectionsonly <0\|1>` | `sco` | Require LE Secure Connections for every service                  |
| `pair`                         | `p`   | Pair and bond with the connected peer                            |
| `passkey <000000-999999>`      | `pk`  | Answer a passkey request                                         |
| `numericcomparison <0\|1>`     | `nc`  | Answer a numeric comparison request                              |
| `removebonds`                  | `rb`  | Remove every stored bond                                         |

### GATT

| Command       | Alias | Description                                            |
|---------------|-------|--------------------------------------------------------|
| `discover`    | `d`   | Discover the Nordic UART service on the connected peer |
| `send <text>` | `s`   | Send text over the Nordic UART service                 |

## Two boards, one link

With `ble_peripheral` on one board and `ble_central` on the other, each on its own terminal:

```text
peripheral> security 4
peripheral> advertise

central> scan
discovered 0a:00:00:e1:80:02 publicAddress advInd rssi -42 name hal-st-periph
central> stopscan
central> security 4
central> connect 0a:00:00:e1:80:02 0
central> pair
central> mtu
central> discover
central> send hello
```

`send` then works in both directions: the central writes into Rx, the peripheral notifies on Tx.
Rx and Tx are named from the peripheral's point of view, which is the one thing about this
profile that is reliably misread.

## Bonds are not persistent

Bonds live in RAM and are gone after a reset; see the same section in the
[`ble_peripheral` README](../ble_peripheral/README.md#bonds-are-not-persistent) for what a
product would put in their place.

## STM32WBA

The example builds and links for `stm32wba55`. On WBA the host stack and link layer run on the
application core as ST's prebuilt archives, and they call back into a platform port that the
application provides. hal-st provides that port in `hal_st/middlewares/ble_middleware`:

- **`BleStackProcessWba.cpp`** runs the link layer background process and the host stack from
  the event dispatcher, which takes the place of ST's sequencer.
- **`BleWrapWba.c`** compiles ST's ACI/HCI wrappers so that each command schedules the host stack.
- **`LinkLayerPlatformWba.cpp`** is the link layer's platform: radio clocks, the radio and
  software low interrupts, interrupt masking, random numbers and the link layer configuration.
- **`PowerTableWba.cpp`** holds ST's TX power tables.
- **`BlePlatformWba.cpp`** holds `BLEPLAT_*`: random numbers, AES-ECB and AES-CMAC on the AES
  peripheral, P-256 key generation and the Diffie-Hellman key on the PKA, the stack's timers on
  `infra::TimerSingleShot`, and its NVM. AES-CCM is a stub; only the stack's Encrypted Advertising
  Data commands use it, and the BLE middleware issues none.
- **`BleNvmWba.cpp`** is the stack's NVM: the RAM cache in which the stack keeps its security and
  GATT database, written back to a `ConfigurationStore` entry each time the stack asks to store it.

`SystemTransportLayerWba` takes its hardware in `SystemTransportLayerWba::HardwareDependencies`:
creators for the RNG (`hal::SynchronousRandomDataGenerator`), the AES (`services::Aes128Ecb`) and
the PKA (`services::EllipticCurveOperations`). The platform creates
each only while it needs it: the RNG while it refills the pool of random words that the link layer
and the host stack draw from, the AES for one encryption, and the PKA from the start of a key
operation until its completion. The application can use the same creators in between, but must not
hold one across a point where the stack may need it; `infra::Creator` asserts on overlapping use.

Its `SystemTransportLayerWba::Config` holds the maximum ATT MTU and the link layer's
`LinkLayerPlatformWba::Config`: the radio sleep timer clock (LSE by default, which the
NUCLEO-WBA55CG has), its accuracy, the TX power table and the interrupt the link layer uses as its
software low interrupt (`HASH_IRQn` by default). The link layer takes over the `RADIO`
vector, and registers the software low interrupt in the interrupt table, so pick one whose peripheral
the application does not drive by interrupts.

`SystemTransportLayerWba` also takes that `ConfigurationStore` entry, a range of
`SystemTransportLayerWba::bondBlobSize` bytes, and loads the cache from it before the stack
starts. A blob written by the record based NVM of hal-st's earlier stack version is discarded, so
bonds stored by that version are lost once. The example hands it a `services::ConfigurationStoreStub`, so bonds do not survive a reset;
see [Bonds are not persistent](../ble_peripheral/README.md#bonds-are-not-persistent).

The example uses `TracingSystemTransportLayerWba`, which traces the stack's version at start-up, as
reported by `SystemTransportLayerWba::GetVersion()`, and when the HCI event queue fills and pauses
the event flow. `DirectTestModeSt::SetTransmitPowerLevel` picks the nearest PA level of ST's table,
up to the maximum of the selected TX power table.

Deep sleep is not supported yet: the link layer never enters its deep sleep, and nothing puts the
device into standby.

None of this affects STM32WB55, where the stack runs on CPU2 and the transport layer in tree is
complete.
