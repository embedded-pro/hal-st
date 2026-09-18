# ble_peripheral

A BLE peripheral driven entirely from a terminal on the ST-Link virtual COM port. It advertises,
accepts a connection, runs the GAP security procedures on demand, and serves a small GATT
database built from the profiles in `services/ble/profile`.

Its counterpart is [`ble_central`](../ble_central/README.md); flashing one board with each gives
a link that can be driven from both ends.

## Targets

| Board          | Preset       | Status                                                              |
|----------------|--------------|---------------------------------------------------------------------|
| NUCLEO-WB55RG  | `stm32wb55`  | Built by default                                                    |
| NUCLEO-WBA55CG | `stm32wba55` | Needs `HALST_BUILD_EXAMPLES_BLE_WBA=On` — see [STM32WBA](#stm32wba) |

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

| Command       | Alias  | Description                                                          |
|---------------|--------|----------------------------------------------------------------------|
| `advertise`   | `adv`  | Start connectable undirected advertising                             |
| `standby`     | `sb`   | Stop advertising                                                     |
| `name <name>` | `n`    | Set the advertised device name and re-publish the advertisement data |
| `address`     | `addr` | Show the public and identity address                                 |
| `state`       | `st`   | Show the link state, the pipe state and the bond count               |

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
`hal::FlashInternalStmBle` — see [STM32WB55 Internal Flash Usage with BLE](../../hal_st/stm32fxxx/STM32WB55_Internal_Flash_Usage_with_BLE.md)
for why the flash driver on WB55 has to negotiate with the radio coprocessor.

## STM32WBA

The example compiles for `stm32wba55` but does not link yet, so
`HALST_BUILD_EXAMPLES_BLE_WBA` stays off. Building with it on
(`cmake --preset stm32wba55 -DHALST_BUILD_EXAMPLES_BLE_WBA=On`) shows what a WBA port still owes:

- **The BLE platform layer is absent.** `BLEPLAT_Init`, `BLEPLAT_AesEcbEncrypt`,
  `BLEPLAT_AesCmacSetKey`, `BLEPLAT_AesCmacCompute`, `BLEPLAT_PkaStartP256Key`, `BLEPLAT_RngGet`,
  `BLEPLAT_NvmAdd`, `BLEPLAT_NvmGet`, `BLEPLAT_NvmDiscard`, `BLEPLAT_TimerStart` and
  `BLEPLAT_TimerStop` are defined in no archive in the tree, and neither is `ll_sys_reset`, which
  STM32CubeWBA puts in the application's `ll_sys_if.c`. `hal_st/stm32fxxx` already has the drivers
  they would sit on — `SynchronousAesStm`, `PkaStm`, `RandomDataGeneratorStm`, `RtcStm`.
- **`LINKLAYER_PLAT_*` and `LINKLAYER_DEBUG_SIGNAL_*` follow.** They are undefined in
  `LinkLayer_BLE_Basic_lib.a` and defined nowhere; they do not show up in today's link only
  because the objects referencing them are never extracted.
- **The prebuilt archives need group ordering.** `stm32wba_ble_stack_basic.a` references
  `ll_intf_*` in `LinkLayer_BLE_Basic_lib.a`, which `hal_st.stm32_wpan_stm32wbaxx.libs` lists
  first, so those symbols come back undefined although they are present. The archives need
  `--start-group`, or the reverse order.

None of this affects STM32WB55, where the stack runs on CPU2 and the transport layer in tree is
complete.
