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

On WB55 the wireless coprocessor has to be running a full BLE stack (not the HCI-only one); flash
it with STM32CubeProgrammer before running this example.

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
