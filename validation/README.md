# Hardware-in-the-loop validation

This directory validates the hal-st drivers on real hardware: a NUCLEO-WB55RG or NUCLEO-WBA55CG runs a validation firmware, and a host PC drives that firmware and a Digilent Analog Discovery 3 (AD3) to stimulate and measure every peripheral.
Every driver is exercised generically, with each variant it has (interrupt, DMA, synchronous, low-power instance) and with every option the firmware exposes.
The structure follows hal-ti's `validation/`; the firmware runs on EMIL's hardware-in-the-loop terminal and the host reuses the generic bench code of ad3-waveforms-bench.

- `firmware/` - C++ firmware on hal-st and EMIL. It exposes the hal-st peripherals through a line-based terminal; the command set is specified in [PROTOCOL.md](PROTOCOL.md).
- `host/` - Python package `hal_st_validation` and a pytest suite that talks to the firmware terminal over a serial port and to the AD3 through the WaveForms SDK.
- The generic bench code (AD3 wrapper over the WaveForms SDK, signal analysis, `OK`/`ERR`/`EVT` terminal client, console, pytest plugin and fakes) lives in the separate [ad3-waveforms-bench](https://github.com/embedded-pro/ad3-waveforms-bench) repository; `hal_st_validation` only adds what is specific to hal-st.
- hal-st has no comparator, CAN, EEPROM or Ethernet driver for these MCUs, so those commands answer `ERR unsupported`; I2C, flash, RNG and AES have no command group in EMIL's terminal yet and are not validated here.

## Why C++ and Python

- The firmware has to be C++: it is built from hal-st and EMIL exactly like an application would use them, so what is validated is the real driver code with the real interrupt table, clocks, DMA and pin muxing.
- The host is Python because Digilent ships the WaveForms SDK with official Python bindings and samples, `pyserial` covers the terminal, and pytest brings parametrisation, fixtures, skips and JUnit/HTML reports for free.
- Python's latency does not matter: every timing-critical stimulus or measurement is done by the AD3 hardware (pattern generator, logic analyzer, wavegen, protocol engines) or by the firmware itself; the host only configures, triggers and evaluates.

## What the AD3 does

| Protocol or signal         | AD3 capability                                                                   | How the tests use it                                                                       |
|----------------------------|----------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------|
| UART                       | Full TX/RX through the SDK protocol UART (`FDwfDigitalUart*`)                    | Peer in both directions; the logic analyzer decodes the firmware TX line and its bit rate  |
| SPI                        | SPI master in the SDK; SPI slave reported in recent WaveForms, unverified in SDK | Firmware is the master: logic-analyzer decode, static MISO level or MOSI-MISO jumper       |
| PWM, encoder, GPIO, WWDG   | Pattern generator, logic analyzer with DIO-edge trigger, static DIO              | Encoder signals, pulse trains, break inputs and CTS; frequency, duty, dead time and periods |
| ADC                        | Wavegen W1/W2 (DC) and scope channels 1/2                                        | Analog levels on the inputs; the scope measures the level actually applied                 |

## Hardware setup

- Connect AD3 GND to the Nucleo GND. All AD3 DIOs are 3.3 V LVCMOS, compatible with the STM32 pins.
- The Nucleo is powered from its ST-LINK USB port. The AD3 V+/V- supplies stay off unless `ad3.vplus`/`ad3.vminus` are set in the board file.
- Wavegen outputs are refused outside `ad3.analog_limits` (0..3.3 V by default) so a wrong parameter cannot overdrive an analog input.
- Run the tests with nothing else connected to the pins of the selected wiring sets: the tests drive them directly.
- Each board has fixed wiring bundles (tables below); wire one, run its tests, then switch:
  - `bundle1` wires all 16 DIOs and W1/W2 on two ADC inputs, and runs nearly the whole suite.
  - `bundle2` (NUCLEO-WB55RG only) moves DIO9/DIO10 to the LPTIM1 inputs PC0/PC2 for the LPTIM encoder tests; W2 and scope 2 are unplugged because PC2 is then driven by a DIO.
- Each pin serves several tests: the TIM1 outputs are also SPI and encoder pins, the LPUART1 CTS pin is also the SPI1 MISO, and so on. The firmware frees every pin between tests, so a pin can change role from one test to the next.
- Tests whose connections are missing from the selected sets are skipped with the reason.
- The terminal is USART1 on the ST-LINK virtual COM port (`/dev/ttyACM0`, `COMx`): PB6/PB7 on the NUCLEO-WB55RG, PB12/PA8 on the NUCLEO-WBA55CG, at 921600 baud.
  If the ST-LINK of a board cannot keep up with 921600 baud, change `terminalBaudRate` in `firmware/boards/<mcu>/BoardProfile.hpp` and `terminal.baud` in the board file together.
- The header positions in the tables come from the Arduino/morpho mapping of the Nucleo-64 boards; those marked "check UM2435" depend on solder bridges and should be checked against the board's user manual before wiring.
- NUCLEO-WB55RG: the firmware runs on the Cortex-M4 alone and never starts the wireless coprocessor; the linker script keeps it below the flash and SRAM the wireless stack uses.

## Build and flash the firmware

The target is `hal_st.validation_firmware`, built with the regular presets (`HALST_BUILD_EXAMPLES` is on in them). Like every embedded preset it needs the host tooling package (built by the `host` preset's `package` target) extracted under `install/` first, as `.github/workflows/ci.yml` does:

```bash
cmake --preset stm32wb55
cmake --build --preset stm32wb55-RelWithDebInfo --target hal_st.validation_firmware
cmake --preset stm32wba55
cmake --build --preset stm32wba55-RelWithDebInfo --target hal_st.validation_firmware
```

Build it optimized (`RelWithDebInfo`): at `-O0` the interrupt-driven drivers cannot keep up with the 921600 baud terminal.
The artifacts are `build/<preset>/validation/firmware/RelWithDebInfo/hal_st.validation_firmware.{elf,bin,hex}`. Flash through the on-board ST-LINK, for example with STM32CubeProgrammer:

```bash
STM32_Programmer_CLI -c port=SWD -w build/stm32wb55/validation/firmware/RelWithDebInfo/hal_st.validation_firmware.hex -v -rst
```

After reset the firmware prints `EVT boot board=... family=... sysclk=... reset=...`, and the debug LED (WB55 blue LD1, WBA55 red LD3) blinks.

## Install the host package

1. Install the Digilent WaveForms application (it contains the WaveForms runtime `dwf` and the SDK) from the Digilent website; on Linux also install the Adept 2 runtime it depends on. `ad3-waveforms-bench` loads `libdwf.so` / `dwf.dll` / `dwf.framework` from the default location; set `DWF_LIBRARY` to override it.
2. Create a virtual environment and install the package (Python 3.10 or newer):

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -e "validation/host[ad3]"
```

This also installs `ad3-waveforms-bench` from `git+https://github.com/embedded-pro/ad3-waveforms-bench@dcb3754` (see `host/pyproject.toml`), the same commit hal-ti uses.
To work on both at the same time, install a local checkout first and then this package without dependencies:

```bash
pip install -e ../ad3-waveforms-bench
pip install -e validation/host --no-deps
```

The `ad3` extra adds no Python dependency: the SDK is reached through `ctypes`, so only the WaveForms runtime must be installed. Without it, everything except the AD3 still works and AD3 tests are skipped.

## Run the tests

Unit tests run without hardware:

```bash
pytest validation/host/tests/unit
```

Hardware tests need `--port`; without it they are skipped. Select the board file and the wiring sets that are actually connected, and start with the quick depth:

```bash
pytest validation/host/tests/hil --board nucleo_wb55rg --port /dev/ttyACM0 --wiring-set bundle1 --depth quick
pytest validation/host/tests/hil --board nucleo_wb55rg --port /dev/ttyACM0 --wiring-set bundle1 --depth full
pytest validation/host/tests/hil/test_qei.py --board nucleo_wb55rg --port /dev/ttyACM0 --wiring-set bundle2
pytest validation/host/tests/hil --board nucleo_wba55cg --port /dev/ttyACM0 --wiring-set bundle1
pytest validation/host/tests/hil --board nucleo_wb55rg --port /dev/ttyACM0 --no-ad3
```

Run with `--depth quick` first; once it passes, run it again with `--depth full`, which takes much longer. Collect the reports with `--junitxml report-<board>-<sets>-<depth>.xml`.

Options from `tests/conftest.py`:

- `--board` - board file name in `host/boards/` or a path to a YAML file (default `nucleo_wb55rg`, or `HAL_ST_BOARD`).
- `--port`, `--baud` - firmware terminal serial port (or `HAL_ST_PORT`) and baud rate (default from the board file, 921600).
- `--command-timeout` - seconds to wait for a reply (or `HAL_ST_COMMAND_TIMEOUT`, default from the board file); raise it for slow links such as port-bridge.
- `--wiring-set a,b` - active wiring sets (or `HAL_ST_WIRING`).
- `--with <tag>` - enable optional wiring (`loopback` for the SPI MOSI-MISO jumper), repeatable.
- `--depth quick|full` - `quick` (default, or `HAL_ST_DEPTH`) runs a pairwise subset of every parameter matrix: every pair of values of any two parameters appears in at least one test. `full` runs the complete cartesian products.
- `--set path=value` - override a test parameter, value parsed as YAML: `--set pwm.waveform.freq=[20000] --set uart.transfer.baud=[921600]`.
- `--run-known-gaps` - also run the tests of known driver gaps that abort or hang the firmware (see [Known driver gaps](#known-driver-gaps)); they are skipped by default.

Options from the `ad3_waveforms_bench` pytest plugin (loaded automatically once the package is installed); `tests/conftest.py` feeds it the `ad3` section of the board file:

- `--ad3-serial` - pick an AD3 by serial number (or `AD3_SERIAL`); `--ad3-remote host[:port]` uses an AD3 on another machine (or `AD3_REMOTE`); `--no-ad3` skips every test that needs it.
- `--fake` - run the HIL plumbing against the in-memory fakes (`FakeDwfApi` for the AD3, `hal_st_validation.fake_firmware` for the terminal); only useful when changing the test code.
  - The fake firmware validates arguments in the firmware's order and models pins, instances, timer sharing, EXTI line ownership, ADC triggers and the watchdog.
  - It models no measured signal, so most tests that read the AD3 fail under `--fake --wiring-set ...`; `--fake --no-ad3` passes completely.

Tests that need no AD3 (system, argument errors, limits, instance and timer sharing, watchdog behaviour) run with any wiring set.
Tests that reset the board on purpose (watchdog, UART swap) are marked `resets_board`; any other unexpected `EVT boot` fails the test that caused it.
Every instance a test opened is closed afterwards and the AD3 outputs are released, so tests are independent (the firmware keeps at most one PWM timer, UART, SPI, ADC, encoder and watchdog open at a time).
Use `-k`, `-m "not slow"` and `--junitxml report.xml` as usual.

## Known driver gaps

Writing the firmware against the drivers showed hal-st bugs that the suite runs into. Each board file lists them under `known_gaps` with the test ids they affect: a gap that aborts or hangs the firmware skips its tests unless `--run-known-gaps` is given, any other is an expected failure (`xfail`, not strict). `--fake` ignores them.

| Board | Driver | Effect | Source |
|-------|--------|--------|--------|
| WBA55 | `SynchronousUartStm` | A send with CTS held off blocks the event loop with no timeout. | `hal_st/synchronous_stm32fxxx/SynchronousUartStm.cpp:62` |
| both  | `UartStm` | `uart.send` completes while up to 9 bytes are still in the TX FIFO and shift register, so a close right after a send can cut them. | `hal_st/stm32fxxx/UartStm.cpp:202-205` |
| both  | `WatchDogStm` | Only the window watchdog exists: timeouts are limited to about 516 ms (WB55) and 330 ms (WBA55), and there is no IWDG driver. | `hal_st/stm32fxxx/WatchDogStm.cpp` |

The gaps found earlier in `PwmStm`, `AdcStm`/`AdcDmaMultiChannelStm`/`AdcTimerTriggeredBase` (WBA55 ADC4), `SpiMasterStm`/`SynchronousSpiMasterStm` (receive-only start), `UartStm` (TXEIE after close, SWAP, overrun), `SynchronousQuadratureEncoderLpTimStm` (filter carry-over) and `GpioStm` (EXTI on port H) are fixed, and the tests that exposed them now guard the fixes. The port H fix has no HIL test: PH3 (BOOT0) is the only port H pin on both boards and is reserved.

Fix the driver, then remove its entry from both board files so the tests guard the fix.

## Windows host and Docker (bridge mode)

Use this when the build and the tests run in the hal-st devcontainer (Docker) but the AD3, the Nucleo's virtual COM port and its ST-LINK are plugged into a Windows PC. Docker cannot reach those USB devices, so Windows shares them over TCP and the container uses them through `host.docker.internal` (the devcontainer maps that name to the host):

```text
 Docker container (devcontainer)                  Windows host
 ┌─────────────────────────────────┐  TCP 5025   ┌─────────────────────────────┐
 │ pytest --ad3-remote ...         │ ──────────▶ │ ad3-bench-server            │── WaveForms ── AD3 (USB)
 │ pytest --port socket://...:5000 │  TCP 5000   │ port-bridge  (serial)       │── COMx ─────── ST-LINK VCP
 │ gdb-multiarch (flash, debug)    │  TCP 61234  │ ST-LINK GDB server          │── ST-LINK ──── Nucleo SWD
 └─────────────────────────────────┘             └─────────────────────────────┘
```

| Port  | Windows service | Used in the container |
|-------|-----------------|-----------------------|
| 5025  | [`ad3-bench-server`](https://github.com/embedded-pro/ad3-waveforms-bench) (WaveForms, AD3) | `--ad3-remote host.docker.internal:5025` or `AD3_REMOTE` |
| 5000  | [`port-bridge`](https://github.com/gabrielfrasantos/port-bridge) serial (firmware terminal) | `--port socket://host.docker.internal:5000` or `HAL_ST_PORT` |
| 61234 | ST-LINK GDB server of STM32CubeIDE / STM32CubeCLT | `gdb-multiarch ... -ex "target remote host.docker.internal:61234"`, and the `stm32wb55rg` / `stm32wba55cg` VS Code launch configurations |

On Windows:

1. Install [WaveForms](https://digilent.com/reference/software/waveforms/waveforms-3/start), plug in the AD3 and check that WaveForms sees it. Close WaveForms afterwards: only one program can own the AD3.
2. Install Python 3.10 or newer, then both bridges (the [ad3-waveforms-bench releases](https://github.com/embedded-pro/ad3-waveforms-bench/releases) and [port-bridge releases](https://github.com/gabrielfrasantos/port-bridge/releases) also provide Windows installers):

   ```powershell
   py -m venv $env:USERPROFILE\hil-bridge
   & $env:USERPROFILE\hil-bridge\Scripts\Activate.ps1
   pip install "ad3-waveforms-bench @ git+https://github.com/embedded-pro/ad3-waveforms-bench@dcb3754fd4a4997e4e78cd824f48bc13b0c2435a"
   pip install "port-bridge @ git+https://github.com/gabrielfrasantos/port-bridge@v0.1.5"
   ```

3. Start them, in separate terminals:

   ```powershell
   ad3-bench-server
   port-bridge --serial-port COM5 --serial-baudrate 921600
   ```

   - `COM5` is the ST-LINK virtual COM port of the Nucleo. The serial bridge is a plain byte stream, so the baud rate is set here and `--baud` has no effect in the container.
   - Start the ST-LINK GDB server (from STM32CubeIDE or STM32CubeCLT) on port 61234, accepting connections from the container.
   - `port-bridge` holds the COM port while it runs; stop it before opening the port in another tool. `ad3-bench-server --fake` serves a simulated AD3 to try the setup without hardware.

Both bridges listen on `127.0.0.1` by default, which Docker Desktop reaches through `host.docker.internal`. If the container cannot connect (for example Docker Engine inside WSL2), listen on all interfaces with `ad3-bench-server --host 0.0.0.0 --token <secret>` (and `AD3_REMOTE_TOKEN=<secret>` in the container) and `port-bridge --bind 0.0.0.0`, and keep ports 5025, 5000 and 61234 blocked from the network in the Windows firewall: whoever reaches them controls the AD3 and the board.

In the container:

```bash
cmake --preset stm32wb55
cmake --build --preset stm32wb55-RelWithDebInfo --target hal_st.validation_firmware

gdb-multiarch build/stm32wb55/validation/firmware/RelWithDebInfo/hal_st.validation_firmware.elf -batch \
    -ex "target remote host.docker.internal:61234" \
    -ex "monitor reset" -ex load -ex "monitor reset" -ex detach

python3 -m venv .venv && . .venv/bin/activate
pip install -e "validation/host[ad3]"

export AD3_REMOTE=host.docker.internal:5025
export HAL_ST_PORT=socket://host.docker.internal:5000
pytest validation/host/tests/hil --board nucleo_wb55rg --wiring-set bundle1 --depth quick
```

## Wiring sets

The tables are generated from the board files; the notes list every function a pin serves and the Arduino/morpho header position. Scope inputs are single ended: connect the `-` input of each used scope channel to GND.

### NUCLEO-WB55RG wiring

| Wiring set | AD3      | Pin               | Note                                                                    |
|------------|----------|-------------------|-------------------------------------------------------------------------|
| `bundle1`  | DIO0     | PA8 (tim1ch1)     | TIM1 CH1, encoder TIM1 A, GPIO (D6)                                     |
| `bundle1`  | DIO1     | PA7 (tim1ch1n)    | TIM1 CH1N, SPI1 MOSI, GPIO (D11)                                        |
| `bundle1`  | DIO2     | PA9 (tim1ch2)     | TIM1 CH2, encoder TIM1 B, GPIO (D9)                                     |
| `bundle1`  | DIO3     | PB8 (tim1ch2n)    | TIM1 CH2N, GPIO (D15)                                                   |
| `bundle1`  | DIO4     | PA10 (tim1ch3)    | TIM1 CH3, TIM17 break, GPIO (D3)                                        |
| `bundle1`  | DIO5     | PB9 (tim1ch3n)    | TIM1 CH3N, TIM17 CH1, GPIO (D14)                                        |
| `bundle1`  | DIO6     | PA5 (spi1clk)     | SPI1 SCK, GPIO (D13)                                                    |
| `bundle1`  | DIO7     | PA6 (spi1miso)    | SPI1 MISO, LPUART1 CTS, TIM16 CH1, GPIO (D12)                           |
| `bundle1`  | DIO8     | PA4 (spi1cs)      | SPI1 chip select, GPIO (D10, see the solder-bridge note)                |
| `bundle1`  | DIO9     | PA15 (qei2a)      | TIM2 CH1, encoder TIM2 A, GPIO (D5)                                     |
| `bundle1`  | DIO10    | PB3 (qei2b)       | TIM2 CH2, encoder TIM2 B, GPIO (CN10, check UM2435)                     |
| `bundle1`  | DIO11    | PC6 (gpio0)       | GPIO/EXTI, encoder TIM2 index, watchdog warning toggle (D2)             |
| `bundle1`  | DIO12    | PA2 (lpuart1tx)   | LPUART1 TX, TIM2 CH3, GPIO (D1)                                         |
| `bundle1`  | DIO13    | PA3 (lpuart1rx)   | LPUART1 RX, TIM2 CH4, GPIO (D0)                                         |
| `bundle1`  | DIO14    | PB12 (lpuart1rts) | LPUART1 RTS, TIM1 break, GPIO (CN10, check UM2435)                      |
| `bundle1`  | DIO15    | PB0 (led0)        | green LED output, GPIO (CN10, check UM2435)                             |
| `bundle1`  | W1       | PC3 (ain4)        | ADC1 IN4 (A4)                                                           |
| `bundle1`  | W2       | PC2 (ain3)        | ADC1 IN3 (A5)                                                           |
| `bundle1`  | Scope 1+ | PC3 (ain4)        |                                                                         |
| `bundle1`  | Scope 2+ | PC2 (ain3)        |                                                                         |
| `bundle1`  | -        | -                 | `--with loopback`: jumper PA7 (SPI1 MOSI, D11) to PA6 (SPI1 MISO, D12); DIO7 then only listens |
| `bundle1`  | -        | -                 | D10 is PA4 only with the default solder bridges (SB41 on, SB42 off; otherwise PB10), check against UM2435 |
| `bundle2`  | DIO0-8, DIO11-15, W1, Scope 1+ | as `bundle1` |                                                       |
| `bundle2`  | DIO9     | PC0 (lptim1in1)   | LPTIM1 IN1, encoder LPTIM1 A, ADC1 IN1 (A0)                             |
| `bundle2`  | DIO10    | PC2 (lptim1in2)   | LPTIM1 IN2, encoder LPTIM1 B, ADC1 IN3 (A5); W2 and scope 2 unplugged   |

### NUCLEO-WBA55CG wiring

| Wiring set | AD3      | Pin               | Note                                                                    |
|------------|----------|-------------------|-------------------------------------------------------------------------|
| `bundle1`  | DIO0     | PB4 (spi1clk)     | SPI1 SCK, TIM1 CH3, blue LED (led0), GPIO (D13)                         |
| `bundle1`  | DIO1     | PB3 (spi1miso)    | SPI1 MISO, TIM1 CH4, TIM17 CH1N, GPIO (D12)                             |
| `bundle1`  | DIO2     | PA15 (spi1mosi)   | SPI1 MOSI, encoder TIM1 index, TIM17 break, GPIO (D11)                  |
| `bundle1`  | DIO3     | PA12 (spi1cs)     | SPI1 chip select, TIM1 CH2, encoder TIM1 B, GPIO (D10)                  |
| `bundle1`  | DIO4     | PA11 (tim1ch1)    | TIM1 CH1, encoder TIM1 A, USART2 RX, GPIO (D4)                          |
| `bundle1`  | DIO5     | PB2 (tim1ch1n)    | TIM1 CH1N, USART2 CTS, GPIO (D15)                                       |
| `bundle1`  | DIO6     | PB1 (tim1ch2n)    | TIM1 CH2N, USART2 RTS, GPIO (D14)                                       |
| `bundle1`  | DIO7     | PB0 (tim1ch3n)    | TIM1 CH3N, USART2 TX, GPIO (D6)                                         |
| `bundle1`  | DIO8     | PB5 (lpuart1tx)   | LPUART1 TX, GPIO (D1)                                                   |
| `bundle1`  | DIO9     | PA10 (lpuart1rx)  | LPUART1 RX, TIM3 CH1, encoder TIM3 A, GPIO (D0)                         |
| `bundle1`  | DIO10    | PB9 (lpuart1rts)  | LPUART1 RTS, TIM3 CH4, TIM16 CH1, GPIO (D7)                             |
| `bundle1`  | DIO11    | PB15 (lpuart1cts) | LPUART1 CTS, TIM16 break, GPIO (D8)                                     |
| `bundle1`  | DIO12    | PA1 (tim3ch2)     | TIM3 CH2, encoder TIM3 B, TIM17 CH1, GPIO (A3)                          |
| `bundle1`  | DIO13    | PB14 (gpio0)      | GPIO/EXTI, TIM3 CH3, encoder TIM3 index, watchdog warning toggle (D5)   |
| `bundle1`  | DIO14    | PA2 (tim1bkin)    | TIM1 break input, GPIO (A2)                                             |
| `bundle1`  | DIO15    | PA5 (tim2ch1)     | TIM2 CH1, GPIO (A4)                                                     |
| `bundle1`  | W1       | PA7 (ain2)        | ADC4 IN2 (A0)                                                           |
| `bundle1`  | W2       | PA6 (ain3)        | ADC4 IN3 (A1)                                                           |
| `bundle1`  | Scope 1+ | PA7 (ain2)        |                                                                         |
| `bundle1`  | Scope 2+ | PA6 (ain3)        |                                                                         |
| `bundle1`  | -        | -                 | `--with loopback`: jumper PA15 (SPI1 MOSI, D11) to PB3 (SPI1 MISO, D12); DIO1 then only listens |
| `bundle1`  | -        | -                 | PB4 (D13) also drives the blue LED LD1, which loads the SPI1 clock line |

## What is tested

- `test_system.py` - `ping`, `info`, and the `board.pins` alias table against the board file in both directions, every alias accepted as a pin, reserved terminal/SWD/LSE/BOOT0 pins and the debug LED, unbonded pins, pin syntax, the terminal UART, error reasons (`usage`, `busy`, `notopen`, `range`, `unsupported`), missing instances (including 0), `delay`, `reset` and the `EVT boot` cause.
- `test_gpio.py` - output levels with every drive (`hal::Speed`), inputs following the AD3 with every pull, pull-only idle levels, open drain, the user LED, eight pins at a time, pins held by other groups, interrupt counts for edge x handler type x pulse count x frequency against exact AD3 pulse trains, one EXTI line per port at a time, `gpio.pulse` timing.
- `test_pwm.py` - for `PwmStm` and `SynchronousPwmStm`:
  - frequency x duty x edge/centre alignment x prescaler, with `ERR range` exactly where the period does not fit the counter (16 or 32 bits);
  - 1-4 channels, complementary outputs and complementary-only outputs with dead time and inversion (shoot-through check), the dead-time limit;
  - idle levels, the break input with both polarities and automatic re-enable;
  - frequency changes and stop, the features each timer offers (`unsupported` elsewhere), argument errors, one timer at a time, and that a re-open forgets dead time and break.
- `test_uart.py` - every baud rate x parity x driver variant (interrupt, DMA, duplex DMA, synchronous) on LPUART1 and USART2, both directions against the AD3 UART, frames decoded from the firmware TX line and the bit rate measured on it, large payloads, full-duplex streaming, RTS/CTS flow control per variant, send timeout with CTS held, re-open with other settings, TX/RX swap, the baud limits of each instance, argument errors and default pins.
- `test_spi.py` - SPI modes 0-3 x baud x driver variant (interrupt, DMA, synchronous) x GPIO chip select, decoded from the logic analyzer (MOSI, MISO, clock polarity and rate, chip-select release), continued sessions, receive-only first transfers, the largest transfer, the baud limits and argument errors.
- `test_adc.py` - wavegen DC levels against raw 12-bit codes (checked against the scope when it is wired), sequences of up to 8 conversions, every sampling time, timer-triggered rates and the measure timeout, timer sharing with PWM and the encoder, unsupported trigger timers, argument errors and limits, and ADC pins against GPIO.
- `test_qei.py` - position counts for frequency x cycles x direction x decoding x phase inversion, physically inverted phases restored by `inva`/`invb`, offset and rollover, speed, the index input through `qei.index`, the LPTIM1 encoder on the NUCLEO-WB55RG, default pins, the resolution limits, argument errors, one encoder at a time and timer sharing.
- `test_watchdog.py` - the window watchdog with timeouts across its range x automatic or manual feeding (warnings, no reset while fed, `reset=wwdg` otherwise), the early-warning period measured on the `pin=` toggle, manual feeding, a single watchdog at a time, the timeout limits and argument errors.
- `test_unsupported.py` - every comparator, CAN, EEPROM and Ethernet command answers `ERR unsupported`.

## Customising

Each board file (`host/boards/<board>.yaml`) holds:

- `terminal`, `clocks` (the kernel clocks the expectations use), `pins` (the PROTOCOL.md alias table, compared with `board.pins`; only the generic names of `hal_st_validation.protocol` are accepted) and `ad3` (supplies, analog limits).
- `wiring_sets` - per set, `dio`, `wavegen` and `scope` maps from AD3 channel to pin or alias. An entry is either a pin or a mapping with `pin`, `jumpered` (pins tied to `pin` with a wire), `role` (a name tests can look up), `note` and `requires` (only used with `--with <tag>`); `jumpers` and `options` document extra wiring.
- `known_gaps` - driver gaps with the test ids (wildcards `*` and `?`) they affect, the reason and whether they hang the firmware.
- `tests` - the parameters of every test module: parameter matrices, pins and instances, levels and tolerances.
  - `@pytest.mark.matrix("pwm.waveform")` turns every key of that mapping into one test parameter of the same name; `@pytest.mark.board_params("argname", "section.key")` adds one parameter from a list.
  - All parameters of a test form one matrix: `--depth full` runs its product, `--depth quick` a pairwise subset (`hal_st_validation.pairwise`); `@pytest.mark.constraint(valid=...)` removes combinations a driver cannot take (for example parity with the synchronous UART).
  - Extending a sweep or moving a peripheral to other pins is a YAML change.

To validate another board, add a board profile under `firmware/boards/<mcu>/` and the MCU to `emil_build_for` in `firmware/CMakeLists.txt`, copy a board file, adapt the pins, wiring sets and parameters, and pass `--board path/to/board.yaml`.

## Interactive console

```bash
hal-st-console --port /dev/ttyACM0
hal-st-console --port /dev/ttyACM0 -c info -c board.pins
```

`hal-st-console` is `ad3-bench-console` from ad3-waveforms-bench with the firmware's 921600 baud and its own history file (`ad3-bench-console --port /dev/ttyACM0` works as well).
The console forwards commands, prints final lines and events, and keeps a history in `~/.hal_st_validation_history`. `:wait <s>` listens for events, `:raw` also shows non-protocol output, `:quit` leaves.

## Package layout

In `hal_st_validation` (hal-st specific):

- `firmware.py` - typed API with one group per PROTOCOL.md section (`fw.system`, `fw.gpio`, `fw.pwm`, `fw.uart`, `fw.spi`, `fw.adc`, `fw.qei`, `fw.wdt`); keyword arguments map 1:1 to protocol options (`continue_` for `continue`).
- `protocol.py` - the hal-st part of the protocol: error reasons, `P<port><index>` pins (ports A-K, index 0-15) and the generic alias names (`normalize_pin`, `parse_pin_map`).
- `config.py` - board file loading, wiring-set merging, parameter matrices, overrides and known gaps; `expect.py` - expected STM32 values (PWM quantisation and range, SPI prescaler, UART baud-rate register limits, WWDG prescaler and period, ADC codes, encoder counts).
- `pairwise.py` - the full product and the deterministic pairwise generator behind `--depth`.
- `fake_firmware.py` - `FakeFirmware`, an in-memory stand-in for the validation firmware used by the unit tests and `--fake`.
- `console.py` - the `hal-st-console` entry point.

In the firmware (`firmware/`):

- `Main.cpp` composes the console, the pin pool and every command group; `Console` is the terminal on USART1; `boards/<mcu>/BoardProfile.hpp` holds the aliases, reserved pins, default pins, DMA request lines, clocks and ADC tables of each board.
- `PinFactoryStm` builds `hal::GpioPinStm`s over the generated pinout tables (bonded pins only, analog sharing, one EXTI line per port); `BoardInfoStm` reports the board, clock and reset cause; `TimerAllocation` keeps PWM, encoder and timer-triggered ADC off each other's timer.
- One factory per command group (`UartFactory`, `SpiFactory`, `AdcFactory`, `PwmFactory`, `QeiFactory` with the `qei.index` command, `WatchDogFactory`) parses the hal-st options and builds the driver; `UnsupportedGroups` answers the rest.
