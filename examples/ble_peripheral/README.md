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

## STM32WBA

The example builds and links for `stm32wba55`, but the BLE stack does not run on it yet. On WBA
the host stack and link layer run on the application core as ST's prebuilt archives, and they
call back into a platform port that the application provides. hal-st provides that port in
`hal_st/middlewares/ble_middleware`, and today it is still made of stubs:

- **`BlePlatformWba.cpp`** holds `BLEPLAT_*`. NVM stores nothing, RNG, AES and CMAC return
  zeros, and PKA and timers report an error.
- **`LinkLayerPlatformWba.cpp`** holds `LINKLAYER_PLAT_*`, `LINKLAYER_DEBUG_SIGNAL_*` and the
  application's `ll_sys_*` hooks. None of them touches the hardware, and nothing drives
  `ll_sys_bg_process` or `BleStack_Process` yet.
- **`PowerTableWba.cpp`** holds ST's TX power tables, which are complete.

These stubs are replaced step by step, in this order:

1. The execution model.
2. The link-layer platform (clocks and radio interrupts).
3. RNG, AES and timers.
4. PKA.
5. NVM for bond persistence.

None of this affects STM32WB55, where the stack runs on CPU2 and the transport layer in tree is
complete.
