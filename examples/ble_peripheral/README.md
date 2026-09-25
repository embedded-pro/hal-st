# ble_peripheral

A BLE peripheral driven entirely from a terminal on the ST-Link virtual COM port. It advertises,
accepts a connection, runs the GAP security procedures on demand, and serves a small GATT
database built from the profiles in `services/ble/profile`.

Its counterpart is [`ble_central`](../ble_central/README.md); flashing one board with each gives
a link that can be driven from both ends.

## Targets

| Board          | Preset       | Status                                                           |
|----------------|--------------|------------------------------------------------------------------|
| NUCLEO-WB55RG  | `stm32wb55`  | Built by default                                                 |
| NUCLEO-WBA55CG | `stm32wba55` | Built by default; not functional yet — see [STM32WBA](#stm32wba) |

```bash
cmake --preset stm32wb55
cmake --build --preset stm32wb55-RelWithDebInfo --target examples_st.ble_peripheral
```

On WB55 the wireless coprocessor has to be running a full BLE stack (not the HCI-only one); flash
it with STM32CubeProgrammer before running this example.

## Terminal

The ST-Link virtual COM port carries both the trace output and the terminal, at 115200 8N1 on
USART1 — PB6/PB7 on WB55, PB12/PA8 on WBA55. `help` lists everything; each command has a short
alias.

### GAP

| Command       | Alias  | Description                                                            |
|---------------|--------|------------------------------------------------------------------------|
| `advertise`   | `adv`  | Start connectable undirected advertising                               |
| `standby`     | `sb`   | Stop advertising                                                       |
| `name <name>` | `n`    | Set the advertised device name, restarting advertising if it is active |
| `address`     | `addr` | Show the public and identity address                                   |
| `state`       | `st`   | Show the link state, the pipe state and the bond count                 |

### Security

| Command                        | Alias | Description                                                      |
|--------------------------------|-------|------------------------------------------------------------------|
| `security <level>`             | `sec` | Security mode 1, level 1 to 4                                    |
| `iocapabilities <caps>`        | `io`  | 0 display, 1 displayYesNo, 2 keyboard, 3 none, 4 keyboardDisplay |
| `secureconnectionsonly <0\|1>` | `sco` | Require LE Secure Connections for every service                  |
| `allowpairing <0\|1>`          | `ap`  | Accept or refuse incoming pairing requests                       |
| `pair`                         | `p`   | Pair and bond with the connected peer                            |
| `passkey <000000-999999>`      | `pk`  | Answer a passkey request                                         |
| `numericcomparison <0\|1>`     | `nc`  | Answer a numeric comparison request                              |
| `removebonds`                  | `rb`  | Remove every stored bond                                         |

Passkey and numeric comparison requests are printed as they arrive, so the sequence is: set the
level and the IO capabilities, connect, `pair`, then answer whatever the peer asks for.

### GATT

| Command           | Alias | Description                                  |
|-------------------|-------|----------------------------------------------|
| `send <text>`     | `s`   | Send text over the Nordic UART service       |
| `battery <0-100>` | `bat` | Set the battery level, notifying subscribers |

## GATT database

- Generic Attribute — Service Changed
- Device Information — manufacturer, model, serial number and firmware revision
- Battery — battery level, read and notify
- Nordic UART — `send` writes into it; a peer writes into Rx and subscribes to Tx

`GattServerSt` reports writes to a characteristic value and nothing else, so the example's
`NordicUartGattServer` decodes two more events the profile needs: a write to the Tx Client
Characteristic Configuration descriptor becomes `NotificationsEnabled`, and the ATT MTU exchange
response becomes `MaxAttMtuSizeChanged`.

## Bonds are not persistent

Bonds live in RAM and are gone after a reset. The example pairs `hal::BondStorageSt`, which is
the controller's own bond database, with a `VolatileBondStorage` in place of the flash-backed
store a product would use, and hands the BLE NVM blob a `services::ConfigurationStoreStub`.

To make bonds survive a reset, replace both with a `services::ConfigurationStoreImpl` on
`hal::FlashCoordinatedWithWirelessStack` over `hal::FlashHomogeneousInternalStm` — see [STM32WB55 Internal Flash Usage with BLE](../../hal_st/stm32fxxx/STM32WB55_Internal_Flash_Usage_with_BLE.md)
for why the flash driver on WB55 has to negotiate with the radio coprocessor.

On WBA the stack runs on the application core, so the `services::ConfigurationStoreImpl` can sit
directly on `hal::FlashHomogeneousInternalStm`; its bond blob is
`hal::SystemTransportLayerWba::bondBlobSize` bytes.

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
  `infra::TimerSingleShot`, and its NVM. AES-CCM is a stub, which the basic stack does not use.
- **`BleNvmWba.cpp`** is the stack's NVM: its security and GATT records, in the record format of
  ST's `nvm_emul.c`, kept in RAM and written back to a `ConfigurationStore` entry after each change.

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
`SystemTransportLayerWba::bondBlobSize` bytes, and loads the records from it before the stack
starts. The example hands it a `services::ConfigurationStoreStub`, so bonds do not survive a reset;
see [Bonds are not persistent](#bonds-are-not-persistent).

The example uses `TracingSystemTransportLayerWba`, which traces the stack's version at start-up, as
reported by `SystemTransportLayerWba::GetVersion()`, and when the HCI event queue fills and pauses
the event flow. `DirectTestModeSt::SetTransmitPowerLevel` picks the nearest PA level of ST's table,
up to the maximum of the selected TX power table.

Deep sleep is not supported yet: the link layer never enters its deep sleep, and nothing puts the
device into standby.

None of this affects STM32WB55, where the stack runs on CPU2 and the transport layer in tree is
complete.
