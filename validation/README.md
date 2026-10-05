# Hardware-in-the-loop validation

This directory validates the hal-st drivers on real hardware: a NUCLEO-WB55RG or NUCLEO-WBA55CG runs a validation firmware, and a host PC drives that firmware and a Digilent Analog Discovery 3 (AD3) to stimulate and measure every peripheral.
Every driver is exercised generically, with each variant it has (interrupt, DMA, synchronous, low-power instance) and with every option the firmware exposes.
The structure follows hal-ti's `validation/`; the firmware runs on EMIL's hardware-in-the-loop terminal and the host reuses the generic bench code of ad3-waveforms-bench.

- `firmware/` - C++ firmware on hal-st and EMIL. It exposes the hal-st peripherals through a line-based terminal; the command set is specified in [PROTOCOL.md](PROTOCOL.md).
- `host/` - Python package `hal_st_validation` and a pytest-bdd suite that talks to the firmware terminal over a serial port and to the AD3 through the WaveForms SDK. The scenarios are Gherkin (`host/tests/hil/features/`), their steps are Python (`host/tests/hil/test_*.py`).
- The generic bench code (AD3 wrapper over the WaveForms SDK, signal analysis, `OK`/`ERR`/`EVT` terminal client, console, pytest plugin and fakes) lives in the separate [ad3-waveforms-bench](https://github.com/embedded-pro/ad3-waveforms-bench) repository; `hal_st_validation` only adds what is specific to hal-st.
- hal-st has no comparator, CAN or Ethernet driver for these MCUs, so those commands answer `ERR unsupported`, as do the command groups one MCU lacks (PROTOCOL.md, "Not available on these boards").
  Peripherals EMIL's terminal has no command group for (I2C, timers, LPTIM, QUADSPI, flash, RNG, AES, PKA and others) get command groups of the validation firmware; the `eeprom` commands are EMIL's, served by an external 24Cxx EEPROM on the I2C bus.

## Why C++ and Python

- The firmware has to be C++: it is built from hal-st and EMIL exactly like an application would use them, so what is validated is the real driver code with the real interrupt table, clocks, DMA and pin muxing.
- The host is Python because Digilent ships the WaveForms SDK with official Python bindings and samples, `pyserial` covers the terminal, and pytest brings parametrisation, fixtures, skips and JUnit/HTML reports for free.
- pytest-bdd runs Gherkin scenarios as ordinary pytest tests, so every test reads as Given/When/Then while the board-file parametrisation, `--depth`, `--set`, `--with`, `--fake`, `known_gaps`, fixtures and reports stay those of pytest.
- Python's latency does not matter: every timing-critical stimulus or measurement is done by the AD3 hardware (pattern generator, logic analyzer, wavegen, protocol engines) or by the firmware itself; the host only configures, triggers and evaluates.

## What the AD3 does

| Protocol or signal       | AD3 capability                                                                   | How the tests use it                                                                        |
|--------------------------|----------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------|
| UART                     | Full TX/RX through the SDK protocol UART (`FDwfDigitalUart*`)                    | Peer in both directions; the logic analyzer decodes the firmware TX line and its bit rate   |
| SPI                      | SPI master in the SDK; SPI slave reported in recent WaveForms, unverified in SDK | Firmware is the master: logic-analyzer decode, static MISO level or MOSI-MISO jumper        |
| PWM, encoder, GPIO, WWDG | Pattern generator, logic analyzer with DIO-edge trigger, static DIO              | Encoder signals, pulse trains, break inputs and CTS; frequency, duty, dead time and periods |
| ADC                      | Wavegen W1/W2 (DC) and scope channels 1/2                                        | Analog levels on the inputs; the scope measures the level actually applied                  |

## Hardware setup

- Connect AD3 GND to the Nucleo GND. All AD3 DIOs are 3.3 V LVCMOS, compatible with the STM32 pins.
- The Nucleo is powered from its ST-LINK USB port. The AD3 V+/V- supplies stay off unless `ad3.vplus`/`ad3.vminus` are set in the board file.
- Wavegen outputs are refused outside `ad3.analog_limits` (0..3.3 V by default) so a wrong parameter cannot overdrive an analog input.
- Run the tests with nothing else connected to the pins of the selected wiring sets: the tests drive them directly.
- Each board has fixed wiring bundles (tables in [Wiring sets](#wiring-sets)); wire one, run its tests, then switch:
  - `bundle1` wires all 16 DIOs and W1/W2 on two ADC inputs, and runs nearly the whole suite.
  - `bundle2` of the NUCLEO-WB55RG moves DIO9/DIO10 to the LPTIM1 inputs PC0/PC2 for the LPTIM encoder tests; W2 and scope 2 are unplugged because PC2 is then driven by a DIO.
  - `bundle2` of the NUCLEO-WBA55CG moves DIO8, DIO9, DIO11 and DIO14 to PA7, PA6, PB8 and PA0 for SPI3, TIM2 CH3/CH4, TIM16 CH1N and the LPTIM1 encoder, and carries the I2C and SPI loop options; W1, W2 and both scopes are unplugged (with `--with i2c` the scopes measure the I2C rise time).
- Optional wiring is fitted on top of a bundle and enabled with `--with <tag>` (see [Optional wiring](#optional-wiring)):
  - `loopback` - an SPI1 MOSI-MISO jumper (both boards, both bundles);
  - `i2c` - I2C1 to I2C3 with external pull-ups and a 24Cxx EEPROM on a breadboard (NUCLEO-WB55RG `bundle1`, NUCLEO-WBA55CG `bundle2`);
  - `spiloop` - SPI1 to SPI2 (NUCLEO-WB55RG `bundle1`) or SPI1 to SPI3 (NUCLEO-WBA55CG `bundle2`); not together with `loopback`.
- Fit an option only together with its `--with <tag>` and remove it otherwise: its wiring loads pins that other tests drive. Start every new setup with the [bench bring-up](#bench-bring-up), which checks the wiring before the suite runs.
- Each pin serves several tests: the TIM1 outputs are also SPI and encoder pins, the LPUART1 CTS pin is also the SPI1 MISO, and so on. The firmware frees every pin between tests, so a pin can change role from one test to the next.
- Tests whose connections are missing from the selected sets are skipped with the reason, and so are tests on pins an enabled option loads ([Which tests skip, and how](#which-tests-skip-and-how)).
- The terminal is USART1 on the ST-LINK virtual COM port (`/dev/ttyACM0`, `COMx`): PB6/PB7 on the NUCLEO-WB55RG, PB12/PA8 on the NUCLEO-WBA55CG, at 921600 baud.
  If the ST-LINK of a board cannot keep up with 921600 baud, change `terminalBaudRate` in `firmware/boards/<mcu>/BoardProfile.hpp` and `terminal.baud` in the board file together.
- The NUCLEO-WB55RG positions come from its user manual, UM2435 Rev 2 (board MB1355C: Table 10, Table 11, Fig. 8 and Fig. 24), cross-checked with the STM32CubeWB example readmes. A later board revision (MB1355D, user manual UM2819) may differ: check the solder bridges named in the table (SB1, SB5, SB8, SB9, SB11, SB12, SB14, SB15, SB41) before wiring.
- The NUCLEO-WBA55CG user manual, UM3301, could not be consulted: its positions come from the STM32CubeWBA example readmes, Zephyr's Arduino connector map and modm-data's transcription of UM3301 Table 8, and its 3V3 and GND pins from a single source (the STM32-Sidewalk-SDK README).
  Each position names its source; check the silkscreen and do the pre-power check of the bring-up before fitting the I2C option.
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

After reset the firmware prints `EVT boot board=... family=... sysclk=... reset=...`, and the debug LED blinks: the blue LD1 on the NUCLEO-WB55RG, the green LD2 on the NUCLEO-WBA55CG, which is not connected on a stock board (SB28 open) and stays dark unless SB28 is closed.

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
pytest validation/host/tests/hil/test_qei.py validation/host/tests/hil/test_timer_pwm.py --board nucleo_wba55cg --port /dev/ttyACM0 --wiring-set bundle2
pytest validation/host/tests/hil --board nucleo_wba55cg --port /dev/ttyACM0 --wiring-set bundle1
pytest validation/host/tests/hil --board nucleo_wb55rg --port /dev/ttyACM0 --no-ad3
```

Run with `--depth quick` first; once it passes, run it again with `--depth full`, which takes much longer. Collect the reports with `--junitxml report-<board>-<sets>-<depth>.xml`.

Options from `tests/conftest.py`:

- `--board` - board file name in `host/boards/` or a path to a YAML file (default `nucleo_wb55rg`, or `HAL_ST_BOARD`).
- `--board-extra <yaml>` - YAML deep-merged over the board file, repeatable (or `HAL_ST_BOARD_EXTRA`, a list separated by `os.pathsep`): mappings merge, lists append, a scalar replaces, and a key ending in `!` replaces the value of the key without it (`limit_pins!: [...]`).
- `--port`, `--baud` - firmware terminal serial port (or `HAL_ST_PORT`) and baud rate (default from the board file, 921600).
- `--command-timeout` - seconds to wait for a reply (or `HAL_ST_COMMAND_TIMEOUT`, default from the board file); raise it for slow links such as port-bridge.
- `--wiring-set a,b` - active wiring sets (or `HAL_ST_WIRING`).
- `--with <tag>` - enable optional wiring, repeatable. The tag must be an option of a selected `--wiring-set`, and options that exclude each other cannot be enabled together; otherwise the run stops with a usage error.
- `--depth quick|full` - `quick` (default, or `HAL_ST_DEPTH`) runs a pairwise subset of every parameter matrix: every pair of values of any two parameters appears in at least one test. `full` runs the complete cartesian products.
- `--set path=value` - override a test parameter, value parsed as YAML: `--set pwm.waveform.freq=[20000] --set uart.transfer.baud=[921600]`.
- `--run-known-gaps` - also run the tests of known driver gaps that abort or hang the firmware (see [Known driver gaps](#known-driver-gaps)); they are skipped by default.

Options from the `ad3_waveforms_bench` pytest plugin (loaded automatically once the package is installed); `tests/conftest.py` feeds it the `ad3` section of the board file:

- `--ad3-serial` - pick an AD3 by serial number (or `AD3_SERIAL`); `--ad3-remote host[:port]` uses an AD3 on another machine (or `AD3_REMOTE`); `--no-ad3` skips every test that needs it.
- `--fake` - run the HIL plumbing against the in-memory fakes (`FakeDwfApi` for the AD3, `hal_st_validation.fake_firmware` for the terminal); only useful when changing the test code.
  - The fake firmware validates arguments in the firmware's order and models pins, instances, timer sharing, the peripherals and DMA channels groups share, EXTI line ownership, ADC triggers and the watchdog; the groups of `fakes/*.py` add the newer command groups.
  - It models no measured signal, so most tests that read the AD3 fail under `--fake --wiring-set ...`; `--fake --no-ad3` passes completely.

Tests that need no AD3 (system, argument errors, limits, instance and timer sharing, watchdog behaviour) run with any wiring set.
Optional wiring loads pins: a test that resolves an AD3 channel (`need.dio`, `need.wavegen`, `need.scope`) for a pin an enabled option loads skips ("pin X loaded by --with T") unless its scenario is tagged `@uses_option:T` or `@requires_option:T`.
Pins an option ties to a channel with a jumper resolve only for such tests. `requires_option` skips the test without the option, `conflicts_option` skips it with the option.
Tests that reset the board on purpose (watchdog, UART swap) are marked `resets_board`; any other unexpected `EVT boot` fails the test that caused it.
Every instance a test opened is closed afterwards and the AD3 outputs are released, so tests are independent (the firmware keeps at most one instance of each group open at a time, see the Framing section of PROTOCOL.md).
Use `-k`, `-m "not slow"` and `--junitxml report.xml` as usual.
`-k` and the `known_gaps` patterns match the test function a scenario is bound to (`test_waveform`, `test_write_read`), so test ids are the same as before the scenarios were written in Gherkin.
`-v --gherkin-terminal-reporter` prints the steps of every scenario, and `--cucumber-json report.json` writes a Cucumber JSON report.

## Known driver gaps

Writing the firmware against the drivers showed hal-st bugs that the suite runs into. Each board file lists them under `known_gaps` with the test ids they affect: a gap that aborts or hangs the firmware skips its tests unless `--run-known-gaps` is given, any other is an expected failure (`xfail`, not strict). `--fake` ignores them.

| Board | Driver               | Effect                                                                                                                             | Source                                                   |
|-------|----------------------|------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------|
| WBA55 | `SynchronousUartStm` | A send with CTS held off blocks the event loop with no timeout.                                                                    | `hal_st/synchronous_stm32fxxx/SynchronousUartStm.cpp:62` |
| both  | `UartStm`            | `uart.send` completes while up to 9 bytes are still in the TX FIFO and shift register, so a close right after a send can cut them. | `hal_st/stm32fxxx/UartStm.cpp:202-205`                   |
| both  | `WatchDogStm`        | Only the window watchdog exists: timeouts are limited to about 516 ms (WB55) and 330 ms (WBA55), and there is no IWDG driver.      | `hal_st/stm32fxxx/WatchDogStm.cpp`                       |

The gaps found earlier in `PwmStm`, `AdcStm`/`AdcDmaMultiChannelStm`/`AdcTimerTriggeredBase` (WBA55 ADC4), `SpiMasterStm`/`SynchronousSpiMasterStm` (receive-only start), `UartStm` (TXEIE after close,
SWAP, overrun), `SynchronousQuadratureEncoderLpTimStm` (filter carry-over) and `GpioStm` (EXTI on port H) are fixed, and the tests that exposed them now guard the fixes. The port H fix has no HIL
test: PH3 (BOOT0) is the only port H pin on both boards and is reserved.

Fix the driver, then remove its entry from both board files so the tests guard the fix.

### Known gaps needing a decision

These behaviours are not fixed here because a fix changes an API or behaviour other users rely on; the tests assert the current behaviour or mark it as a known gap until a decision is made.

- `SpiMasterStm`, `SpiMasterStmDma`, `SynchronousSpiMasterStm` mux `slaveSelect` to NSS but initialise `SPI_NSS_SOFT` and keep SPE on from construction (`SpiMasterStm.cpp:24,56`, `SpiMasterStmDma.cpp:38`, `SynchronousSpiMasterStm.cpp:23`): hardware NSS framing needs SPE toggled per transfer in three drivers; `test_spi_ext.py::test_hardware_nss*` is a known gap.
- `SpiDataSizeConfiguratorStm` with 9- to 16-bit frames makes `SpiMasterStmDma` move 16-bit DMA words (`SpiMasterStmDma.cpp:109-129`), so each frame takes two buffer bytes, little endian; the tests assert that model (`spiwords.spi_frames`) with even lengths.
  An odd byte count has no defined result: the last byte is dropped on the STM32WB55, and the GPDMA of the STM32WBA55 gets a count that is not a multiple of its 16-bit data width.
- `QuadSpiStm::PollStatus` waits with `HAL_MAX_DELAY` (`QuadSpiStm.cpp:83`): a `PollStatus` that never matches hangs the firmware; `test_qspi.py::test_poll_timeout[variant=poll]` is a hanging known gap.
- `AnalogToDigitalPinImplStm` and `AnalogToDigitalInternalTemperatureStm` ignore `numberOfSamples` (`AnalogToDigitalPinStm.cpp:90-165`) and always take one sample; the tests assert one.
- The default sampling time of `AnalogToDigitalInternalTemperatureStm` (`AnalogToDigitalPinStm.hpp:21-27`) is below the temperature sensor's minimum; the tests pass a long `sampling=` and check only that the default delivers a code.
- `TransmitDmaBridgeChannel` and `ReceiveDmaBridgeChannel` (`DmaStm.cpp:1176-1203`) never start on DMA v1 (WB55), the transmit bridge copies destination to source on GPDMA, and the memory side keeps byte width and increment; there is no in-tree user and no command, so they stay untested.
- `LpTimerPwmStm` writes CCR = ARR x duty / 100 with the output polarity high, which may give a duty of 100 - d on the LPTIM; the tests assert d, and if the bench measures 100 - d the test becomes a known gap.

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

| Port  | Windows service                                                                             | Used in the container                                                                                                                    |
|-------|---------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| 5025  | [`ad3-bench-server`](https://github.com/embedded-pro/ad3-waveforms-bench) (WaveForms, AD3)  | `--ad3-remote host.docker.internal:5025` or `AD3_REMOTE`                                                                                 |
| 5000  | [`port-bridge`](https://github.com/gabrielfrasantos/port-bridge) serial (firmware terminal) | `--port socket://host.docker.internal:5000` or `HAL_ST_PORT`                                                                             |
| 61234 | ST-LINK GDB server of STM32CubeIDE / STM32CubeCLT                                           | `gdb-multiarch ... -ex "target remote host.docker.internal:61234"`, and the `stm32wb55rg` / `stm32wba55cg` VS Code launch configurations |

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

Both bridges listen on `127.0.0.1` by default, which Docker Desktop reaches through `host.docker.internal`. If the container cannot connect (for example Docker Engine inside WSL2), listen on all
interfaces with `ad3-bench-server --host 0.0.0.0 --token <secret>` (and `AD3_REMOTE_TOKEN=<secret>` in the container) and `port-bridge --bind 0.0.0.0`, and keep ports 5025, 5000 and 61234 blocked from
the network in the Windows firewall: whoever reaches them controls the AD3 and the board.

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

The tables follow the `wiring_sets` of the board files (keep both in step); the notes list every function a pin serves, and every header position names its source. Scope inputs are single ended: connect the `-` input of each used scope channel to GND.

### Physical rules

- One dupont housing per header pin. AD3 leads (female) sit on male morpho pins; jumper ends go to the free female Arduino sockets (male-ended wire) or to free morpho pins (female-ended wire).
- Where the only header position of a pin already carries an AD3 lead (NUCLEO-WB55RG PB12 at CN10-16, NUCLEO-WBA55CG PB8 at CN4-38), the net goes through a breadboard row: one male-female wire from the header pin to the row, the AD3 lead on a male pin in that row, and the jumper into the same row.
- The I2C option is built on a breadboard: an SCL row, an SDA row, a 3V3 rail and a GND rail. Each rail is fed by one wire from one named header pin; the pull-ups, the EEPROM VCC and GND and (NUCLEO-WBA55CG) the AD3 ground share the rail, never a header pin.
- Fit option wiring only together with its `--with <tag>`, and remove it otherwise. `test_wiring.py` fails when it finds the jumpers of an option that is not enabled (`pass --with <tag> or remove the wiring`).

### Parts for the I2C option

- EEPROM: Microchip 24LC256 or AT24C256 (DIP-8, 3.3 V, 32 KiB, 64-byte pages, 16-bit word address, 400 kHz, 5 ms write cycle). Pins 1-3 (A0-A2) to the GND rail (address 0x50), 4 (VSS) to the GND rail, 5 (SDA) to the SDA row, 6 (SCL) to the SCL row, 7 (WP) to the GND rail (writes enabled), 8 (VCC) to the 3V3 rail; 100 nF from VCC to GND at the part.
- Two 4.7 kOhm resistors (SCL row to the 3V3 rail, SDA row to the 3V3 rail), and two 2.2 kOhm as a fallback.
- A half-size breadboard, 8 male-male and 8 male-female dupont wires, a strip of 2.54 mm male pin headers (to put AD3 leads and scope probes into breadboard rows) and a multimeter.
- Rise time: a NUCLEO-WB55RG bus line (one AD3 DIO, two MCU pins, the EEPROM and the breadboard, about 50 pF) rises in about 0.85 x 4.7 kOhm x 50 pF = 200 ns, under the 300 ns of Fast mode. The NUCLEO-WBA55CG `bundle2` lines carry two AD3 DIOs and a scope probe each (80-100 pF, 320-400 ns): `test_rise_time` measures them; when it fails, fit the 2.2 kOhm resistors (150-190 ns).
- The board file's `tests.eeprom` holds `addr`, `size`, `page`, `abytes`, `wcycle_ms` and `erase_size`, so another part is a YAML change (a 24C02: `size: 256`, `page: 8`, `abytes: 1`).

### Bench bring-up

1. With the board unplugged, wire the bundle and the options you will enable; keep the terminal, SWD and BOOT0 pins free.
2. For `--with i2c`, build the breadboard without the EEPROM, plug the board in and measure 3.3 V between the 3V3 and GND rails (multimeter, or AD3 scope 1 with its leads on the rails). Unplug, insert the EEPROM, plug in again.
3. Flash the firmware and run the wiring self-check first, with the sets and options of the run that follows:

   ```bash
   pytest validation/host/tests/hil/test_wiring.py --board nucleo_wb55rg --port /dev/ttyACM0 --wiring-set bundle1 --with i2c --with spiloop
   pytest validation/host/tests/hil/test_wiring.py --board nucleo_wba55cg --port /dev/ttyACM0 --wiring-set bundle2 --with i2c
   ```

   With the AD3 outputs and pulls off it uses GPIO commands only: each jumper of an enabled option conducts both ways, its pull-ups sit on a live 3V3 rail, the jumpers of offered options that are not enabled are absent, and the pins of `tests.wiring.undriven` follow both MCU pulls (NUCLEO-WBA55CG: no solder bridge connects an ST-LINK line to PA0, PB9, PA10, PB5 or PB15).
4. Run the suite with the same `--wiring-set` and `--with`, `--depth quick` first:

   ```bash
   pytest validation/host/tests/hil --board nucleo_wb55rg --port /dev/ttyACM0 --wiring-set bundle1 --with i2c --with spiloop --depth quick
   pytest validation/host/tests/hil --board nucleo_wba55cg --port /dev/ttyACM0 --wiring-set bundle2 --with i2c --with spiloop --depth quick
   ```

### NUCLEO-WB55RG wiring

`CN7-n`/`CN10-n` are the male ST morpho pins and the easiest to reach with AD3 flywires; `Dn`/`An` are the same nets on the female Arduino sockets.
"via SBn" names a solder bridge that is closed on a stock board. PB3 (SWO) is on no header of a stock board, so TIM2 CH2 and the TIM2 encoder B use PA1. Positions are from UM2435 Rev 2: Table 10 (Arduino), Table 11 (morpho), Fig. 8 (solder bridges) and Fig. 24.

| Wiring set | AD3             | Pin               | Nucleo header                          | Note                                                                                                               |
|------------|-----------------|-------------------|----------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| both       | GND             | GND               | CN10-9 and CN10-20                     | common ground, at least two leads                                                                                  |
| `bundle1`  | DIO0            | PA8 (tim1ch1)     | CN10-25 (D6)                           | TIM1 CH1, encoder TIM1 A, MCO, GPIO                                                                                |
| `bundle1`  | DIO1            | PA7 (tim1ch1n)    | CN10-15 via SB1 (D11)                  | TIM1 CH1N, SPI1 MOSI, I2C3 SCL, QUADSPI IO2, GPIO                                                                  |
| `bundle1`  | DIO2            | PA9 (tim1ch2)     | CN10-19 via SB8 (D9)                   | TIM1 CH2, encoder TIM1 B, I2C1 SCL, GPIO                                                                           |
| `bundle1`  | DIO3            | PB8 (tim1ch2n)    | CN10-3 (D15)                           | TIM1 CH2N, I2C1 SCL, QUADSPI IO1, GPIO                                                                             |
| `bundle1`  | DIO4            | PA10 (tim1ch3)    | CN10-31 via SB11 (D3)                  | TIM1 CH3, TIM17 break, I2C1 SDA, GPIO                                                                              |
| `bundle1`  | DIO5            | PB9 (tim1ch3n)    | CN10-5 (D14)                           | TIM1 CH3N, TIM17 CH1, I2C1 SDA, QUADSPI IO0, GPIO; CN10-6 next to it is the terminal RX (PB7)                      |
| `bundle1`  | DIO6            | PA5 (spi1clk)     | CN10-11 (D13)                          | SPI1 SCK, GPIO                                                                                                     |
| `bundle1`  | DIO7            | PA6 (spi1miso)    | CN10-13 (D12)                          | SPI1 MISO, LPUART1 CTS, TIM16 CH1, QUADSPI IO3, EXTI line 6 partner of PC6, GPIO                                   |
| `bundle1`  | DIO8            | PA4 (spi1cs)      | CN10-17 via SB5 (D10 via SB41)         | SPI1 chip select and NSS, GPIO                                                                                     |
| `bundle1`  | DIO9            | PA15 (qei2a)      | CN10-27 (D5)                           | TIM2 CH1, encoder TIM2 A, GPIO                                                                                     |
| `bundle1`  | DIO10           | PA1 (qei2b)       | CN7-32 via SB14 (A2)                   | TIM2 CH2, encoder TIM2 B, GPIO                                                                                     |
| `bundle1`  | DIO11           | PC6 (gpio0)       | CN10-33 (D2)                           | GPIO/EXTI, encoder TIM1 and TIM2 index, watchdog warning toggle; CN10-34 next to it is the terminal TX (PB6)       |
| `bundle1`  | DIO12           | PA2 (lpuart1tx)   | CN10-35 via SB15 (D1)                  | LPUART1 TX, TIM2 CH3, QUADSPI NCS, GPIO                                                                            |
| `bundle1`  | DIO13           | PA3 (lpuart1rx)   | CN10-37 (D0)                           | LPUART1 RX, TIM2 CH4, QUADSPI CLK, GPIO                                                                            |
| `bundle1`  | DIO14           | PB12 (lpuart1rts) | CN10-16                                | LPUART1 RTS, TIM1 break, SPI2 NSS, GPIO; with `--with spiloop` the lead sits in the PB12 breadboard row            |
| `bundle1`  | DIO15           | PB0 (led0)        | CN10-22                                | green LED2 output, GPIO; LED2 and its 680 ohm resistor load the pin                                                |
| `bundle1`  | W1              | PC3 (ain4)        | CN7-36 (A4)                            | ADC1 IN4                                                                                                           |
| `bundle1`  | W2              | PC2 (ain3)        | CN7-38 (A5)                            | ADC1 IN3                                                                                                           |
| `bundle1`  | Scope 1+        | PC3 (ain4)        | A4 (CN8-5)                             | same net as W1                                                                                                     |
| `bundle1`  | Scope 2+        | PC2 (ain3)        | A5 (CN8-6)                             | same net as W2                                                                                                     |
| `bundle1`  | Scope 1-, 2-    | GND               | CN7-19, CN7-20 (or CN7-8)              | not CN7-22: Fig. 24 calls it GND, the schematic leaves it unconnected                                              |
| `bundle1`  | -               | -                 | -                                      | D10 and CN10-17 carry PA4 with the default solder bridges (SB41 and SB5 closed, SB42 and SB6 open; PB10 otherwise) |
| `bundle1`  | -               | -                 | CN10-6, CN10-34, CN7-7, CN7-13, CN7-15 | keep free: terminal RX/TX to the ST-LINK (PB7, PB6), BOOT0 (PH3) and SWD (PA13, PA14)                              |
| `bundle2`  | everything else | as `bundle1`      |                                        | including GND, the loopback option and the D10 note                                                                |
| `bundle2`  | DIO9            | PC0 (lptim1in1)   | CN7-28 (A0)                            | LPTIM1 IN1, encoder LPTIM1 A, ADC1 IN1                                                                             |
| `bundle2`  | DIO10           | PC2 (lptim1in2)   | CN7-38 (A5)                            | LPTIM1 IN2, encoder LPTIM1 B, ADC1 IN3; W2 and scope 2 unplugged                                                   |

### NUCLEO-WBA55CG wiring

The board manual, UM3301, could not be consulted, so every position names its source:

- Arduino labels (Dn, An) are from Zephyr's `boards/st/nucleo_wba55cg/arduino_r3_connector.dtsi`.
- Morpho positions (CN3, CN4) and Arduino socket positions (CN6, CN7, CN8) are from the STM32CubeWBA v1.10.0 example readmes of `Projects/NUCLEO-WBA55CG`; "readmes (n)" is the number of readmes that give the position.
- CN4-35 and CN4-37 are from modm-data's transcription of UM3301 Table 8; 3V3 (CN5-4) and GND (CN5-6) are from the STM32-Sidewalk-SDK README (`NUCLEO-WBAxx`: `VDD 3V3 | CN5, pin 4`, `GND | CN5, pin 6`), a single source.
- The A1 and A4 sockets are named by their label only; DIO11 and DIO13 have no sourced morpho position and use a male pin in their Arduino socket. Every other AD3 lead sits on a morpho pin, so the Arduino sockets stay free for the option jumpers.

Check before wiring (modm-data, UM3301 Table 8): on MB1801, SB7/SB8 connect PA10/PB5 to the ST-LINK VCP2; SB25 with MB1803 SB8 on and SB9 off routes STLINK_RTS to PA0 (with MB1803 SB32 on and SB31 off to PB15); SB23/SB25 route STLINK_CTS to PB9.
None is the stock setting; `test_wiring.py::test_no_foreign_drivers` checks that PA0, PB9, PA10, PB5 and PB15 follow the MCU pulls with the AD3 released.

Board loads: PB4 drives the blue LD1, which loads the SPI1 clock line. PB8 carries the red LD3 (active low, Zephyr `nucleo_wba55cg.dts`): the LED and its resistor pull PB8 towards 3V3, so PB8 stays out of the pull-level and idle assertions and the `bundle2` TIM16 entry has no break input.
PA9 is the green LD2, the firmware's debug LED, which stays dark unless SB28 is closed (open on a stock board).

| Wiring set | AD3             | Pin                          | Nucleo header                     | Source                    | Note                                                                                                                                                                                              |
|------------|-----------------|------------------------------|-----------------------------------|---------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| both       | GND             | GND                          | CN5-6                             | Sidewalk SDK README       | common ground; CN5-6 is the only sourced GND pin, so scope 1-/2- share it through a breadboard GND rail fed from CN5-6 (with `--with i2c`, the rail of the option)                                |
| `bundle1`  | DIO0            | PB4 (spi1clk)                | CN4-6 (D13)                       | readmes (15)              | SPI1 SCK, TIM1 CH3, blue LED LD1 (led0), GPIO                                                                                                                                                     |
| `bundle1`  | DIO1            | PB3 (spi1miso)               | CN4-10 (D12)                      | readmes (13)              | SPI1 MISO, I2C1 SDA, TIM1 CH4, TIM17 CH1N, GPIO                                                                                                                                                   |
| `bundle1`  | DIO2            | PA15 (spi1mosi)              | CN4-15 (D11)                      | readmes (12)              | SPI1 MOSI, I2C1 SCL, LPTIM1 CH2, encoder TIM1 index, TIM17 break, GPIO                                                                                                                            |
| `bundle1`  | DIO3            | PA12 (spi1cs)                | CN4-17 (D10)                      | readmes (10)              | SPI1 chip select and NSS, TIM1 CH2, encoder TIM1 B, GPIO                                                                                                                                          |
| `bundle1`  | DIO4            | PA11 (tim1ch1)               | CN4-24 (D4)                       | readmes (7)               | TIM1 CH1, encoder TIM1 A, USART2 RX, LPTIM2 CH1, GPIO                                                                                                                                             |
| `bundle1`  | DIO5            | PB2 (tim1ch1n)               | CN4-3 (D15)                       | readmes (31)              | TIM1 CH1N, USART2 CTS, I2C1 SCL, I2C3 SCL, GPIO                                                                                                                                                   |
| `bundle1`  | DIO6            | PB1 (tim1ch2n)               | CN4-5 (D14)                       | readmes (31)              | TIM1 CH2N, USART2 RTS, I2C1 SDA, I2C3 SDA, GPIO                                                                                                                                                   |
| `bundle1`  | DIO7            | PB0 (tim1ch3n)               | CN4-25 (D6)                       | readmes (1)               | TIM1 CH3N, USART2 TX, encoder LPTIM2 B (LPTIM2 IN2), GPIO                                                                                                                                         |
| `bundle1`  | DIO8            | PB5 (lpuart1tx)              | CN4-35 (D1)                       | modm-data, UM3301 Table 8 | LPUART1 TX, GPIO                                                                                                                                                                                  |
| `bundle1`  | DIO9            | PA10 (lpuart1rx)             | CN4-37 (D0)                       | modm-data, UM3301 Table 8 | LPUART1 RX, TIM3 CH1, encoder TIM3 A, GPIO                                                                                                                                                        |
| `bundle1`  | DIO10           | PB9 (lpuart1rts)             | CN4-23 (D7)                       | readmes (4)               | LPUART1 RTS, TIM3 CH4, TIM16 CH1, encoder LPTIM2 A (LPTIM2 IN1), GPIO                                                                                                                             |
| `bundle1`  | DIO11           | PB15 (lpuart1cts)            | male pin in the D8 socket         | Zephyr label              | LPUART1 CTS, TIM16 break, GPIO                                                                                                                                                                    |
| `bundle1`  | DIO12           | PA1 (tim3ch2)                | CN3-34 (A3)                       | readmes (1)               | TIM3 CH2, encoder TIM3 B, TIM17 CH1, LPTIM2 CH2, GPIO                                                                                                                                             |
| `bundle1`  | DIO13           | PB14 (gpio0)                 | male pin in the D5 socket         | Zephyr label              | GPIO/EXTI, TIM3 CH3, encoder TIM3 index, watchdog warning toggle                                                                                                                                  |
| `bundle1`  | DIO14           | PA2 (tim1bkin)               | CN3-32 (A2)                       | readmes (8)               | TIM1 break input, GPIO                                                                                                                                                                            |
| `bundle1`  | DIO15           | PA5 (tim2ch1)                | CN3-36 (A4)                       | readmes (7)               | TIM2 CH1, GPIO                                                                                                                                                                                    |
| `bundle1`  | W1              | PA7 (ain2)                   | CN3-28 (A0)                       | readmes (23)              | ADC4 IN2                                                                                                                                                                                          |
| `bundle1`  | W2              | PA6 (ain3)                   | CN3-30 (A1)                       | readmes (10)              | ADC4 IN3                                                                                                                                                                                          |
| `bundle1`  | Scope 1+        | PA7 (ain2)                   | male pin in the A0 socket (CN7-1) | readmes (12)              | same net as W1                                                                                                                                                                                    |
| `bundle1`  | Scope 2+        | PA6 (ain3)                   | male pin in the A1 socket         | Zephyr label              | same net as W2                                                                                                                                                                                    |
| `bundle1`  | Scope 1-, 2-    | GND                          | GND rail fed from CN5-6           | Sidewalk SDK README       | see GND                                                                                                                                                                                           |
| `bundle2`  | everything else | as `bundle1`                 |                                   |                           | including GND and the loopback option; W1, W2 and the scopes of `bundle1` unplugged. DIO1 (PB3) is also the encoder LPTIM1 B (LPTIM1 IN2), DIO10 (PB9) the SPI3 MISO and DIO15 (PA5) the SPI3 NSS |
| `bundle2`  | DIO8            | PA7 (i2c3sda)                | CN3-28 (A0)                       | readmes (23)              | I2C3 SDA, TIM2 CH3, GPIO                                                                                                                                                                          |
| `bundle2`  | DIO9            | PA6 (i2c3scl)                | CN3-30 (A1)                       | readmes (10)              | I2C3 SCL, TIM2 CH4, GPIO                                                                                                                                                                          |
| `bundle2`  | DIO11           | PB8 (spi3mosi)               | CN4-38                            | readmes (12)              | SPI3 MOSI, TIM16 CH1N, GPIO; the red LD3 loads the pin; with `--with spiloop` the lead sits in the PB8 breadboard row                                                                             |
| `bundle2`  | DIO14           | PA0 (spi3clk)                | CN3-38 (A5)                       | readmes (3)               | SPI3 SCK, encoder LPTIM1 A (LPTIM1 IN1), GPIO                                                                                                                                                     |
| `bundle2`  | Scope 1+, 2+    | PB2 (i2c1scl), PB1 (i2c1sda) | male pins in the SCL and SDA rows | -                         | only with `--with i2c` (rise time); 1- and 2- on the GND rail                                                                                                                                     |

### Optional wiring

The options a set offers are in its `options` (board file); `--with <tag>` enables one, and a tag no selected set offers, or two options that exclude each other, stop the run with a usage error.
The table lists the pins each option loads (tests on them skip unless they use the option) and pulls up; the tables after it give, per net, the AD3 lead already on it, where the jumper or breadboard wire goes, and the source of each position.

| Board          | Option     | Sets                 | Loads                                    | Pull-ups           | Excludes   |
|----------------|------------|----------------------|------------------------------------------|--------------------|------------|
| NUCLEO-WB55RG  | `loopback` | `bundle1`, `bundle2` | PA6, PA7                                 | -                  | `spiloop`  |
| NUCLEO-WB55RG  | `i2c`      | `bundle1`            | PB8, PB9, PC0, PC1                       | PB8, PB9, PC0, PC1 | -          |
| NUCLEO-WB55RG  | `spiloop`  | `bundle1`            | PA4-PA7, PB12-PB15                       | -                  | `loopback` |
| NUCLEO-WBA55CG | `loopback` | `bundle1`, `bundle2` | PA15, PB3                                | -                  | `spiloop`  |
| NUCLEO-WBA55CG | `i2c`      | `bundle2`            | PB2, PB1, PA6, PA7                       | PB2, PB1, PA6, PA7 | -          |
| NUCLEO-WBA55CG | `spiloop`  | `bundle2`            | PB4, PA0, PB3, PB9, PA15, PB8, PA12, PA5 | -                  | `loopback` |

#### NUCLEO-WB55RG `--with loopback` (`bundle1`, `bundle2`)

One male-male wire from D11 to D12; the SPI tests then leave DIO7 an input and check the read-back.

| Net             | AD3 lead        | Jumper or breadboard end | Source          |
|-----------------|-----------------|--------------------------|-----------------|
| PA7 (SPI1 MOSI) | DIO1 at CN10-15 | D11 socket (CN5-4)       | UM2435 Table 10 |
| PA6 (SPI1 MISO) | DIO7 at CN10-13 | D12 socket (CN5-5)       | UM2435 Table 10 |

#### NUCLEO-WB55RG `--with i2c` (`bundle1`)

I2C1 to I2C3 with the EEPROM, ST's one-board pairing PB8-PC0 and PB9-PC1 (STM32CubeWB `Projects/P-NUCLEO-WB55.Nucleo/Examples_LL/I2C/I2C_OneBoard_Communication_IT/readme.txt`). `bundle2` cannot take it: its DIO9 sits on PC0.

| Net                     | AD3 lead                   | Jumper or breadboard end                                       | Source                                                                                     |
|-------------------------|----------------------------|----------------------------------------------------------------|--------------------------------------------------------------------------------------------|
| SCL row: PB8 (I2C1 SCL) | DIO3 at CN10-3 (unchanged) | D15 socket (CN5-10) to the SCL row                             | UM2435 Table 10; CN10-3 Table 11                                                           |
| SCL row: PC0 (I2C3 SCL) | -                          | A0 socket (CN8-1) to the SCL row                               | UM2435 Table 10                                                                            |
| SDA row: PB9 (I2C1 SDA) | DIO5 at CN10-5 (unchanged) | D14 socket (CN5-9) to the SDA row                              | UM2435 Table 10; CN10-5 Table 11                                                           |
| SDA row: PC1 (I2C3 SDA) | -                          | A1 socket (CN8-2) to the SDA row                               | UM2435 Table 10                                                                            |
| 3V3 rail                | -                          | CN6-4 (3V3, female) to the rail                                | UM2435 Table 10; 7.5.2 "3V3 on CN6 pin 4 or CN7 pin 16 can be used as power supply output" |
| GND rail                | -                          | CN6-6 (GND, female) to the rail; CN6-7 spare                   | UM2435 Table 10 (CN6 pins 6 and 7)                                                         |
| Pull-ups                | -                          | 4.7 kOhm from the SCL row and from the SDA row to the 3V3 rail | [parts](#parts-for-the-i2c-option)                                                         |
| EEPROM                  | -                          | as in the parts list                                           | [parts](#parts-for-the-i2c-option)                                                         |

The AD3 observes SCL on DIO3 and SDA on DIO5. Both scope channels stay on the ADC inputs, so the rise time is measured on the NUCLEO-WBA55CG, whose bus has the same pull-up network.
Without the option the standalone I2C tests run on `bundle1` with the MCU pull-ups (`pull=up`): I2C1 on PB8/PB9 (DIO3/DIO5) and on PA9/PA10 (DIO2/DIO4), and I2C3 with SCL on PA7 (DIO1) and SDA on PB4 (CN10-4, UM2435 Table 11; no DIO, so SCL timing and the address NACK only).

#### NUCLEO-WB55RG `--with spiloop` (`bundle1`)

SPI1 to SPI2, ST's one-board pairing (STM32CubeWB `Projects/P-NUCLEO-WB55.Nucleo/Examples_LL/SPI/SPI_OneBoard_HalfDuplex_IT_Init/readme.txt`). SPI2 is observed through DIO6, DIO7, DIO1 and DIO14.

| Net               | AD3 lead                                                           | Jumper or breadboard end                                                                                                        | Source                               |
|-------------------|--------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------|--------------------------------------|
| SCK: PA5 to PB13  | DIO6 at CN10-11                                                    | D13 socket (CN5-6) to CN10-30 (PB13, via SB12)                                                                                  | UM2435 Table 10; Table 11 and Fig. 8 |
| MISO: PA6 to PB14 | DIO7 at CN10-13                                                    | D12 socket (CN5-5) to CN10-28 (PB14)                                                                                            | UM2435 Table 10; Table 11            |
| MOSI: PA7 to PB15 | DIO1 at CN10-15                                                    | D11 socket (CN5-4) to CN10-26 (PB15, via SB9)                                                                                   | UM2435 Table 10; Table 11 and Fig. 8 |
| NSS: PA4 to PB12  | DIO8 at CN10-17; DIO14 moved from CN10-16 to a male pin in the row | D10 socket (CN5-3, PA4 via SB41) to a breadboard row; CN10-16 (PB12, its only position) to the same row with a male-female wire | UM2435 Table 10; Table 11 and Fig. 8 |

#### NUCLEO-WBA55CG `--with loopback` (`bundle1`, `bundle2`)

One male-male wire from D11 to D12; the SPI tests then leave DIO1 an input and check the read-back.

| Net              | AD3 lead       | Jumper or breadboard end | Source                     |
|------------------|----------------|--------------------------|----------------------------|
| PA15 (SPI1 MOSI) | DIO2 at CN4-15 | D11 socket (CN6-4)       | Zephyr label; readmes (14) |
| PB3 (SPI1 MISO)  | DIO1 at CN4-10 | D12 socket (CN6-5)       | Zephyr label; readmes (11) |

#### NUCLEO-WBA55CG `--with i2c` (`bundle2`)

I2C1 (PB2/PB1) to I2C3 (PA6/PA7) with the EEPROM, ST's pairing (STM32CubeWBA `Projects/NUCLEO-WBA55CG/Examples_MIX/I2C/I2C_OneBoard_ComSlave7_10bits_IT/README.md`). `bundle1` cannot take it: its W1/W2 sit on PA7/PA6.

| Net                     | AD3 lead                                                                            | Jumper or breadboard end           | Source                                        |
|-------------------------|-------------------------------------------------------------------------------------|------------------------------------|-----------------------------------------------|
| SCL row: PB2 (I2C1 SCL) | DIO5 at CN4-3                                                                       | D15 socket (CN6-10) to the SCL row | Zephyr label; readmes (2)                     |
| SCL row: PA6 (I2C3 SCL) | DIO9 at CN3-30                                                                      | A1 socket to the SCL row           | Zephyr label only                             |
| SDA row: PB1 (I2C1 SDA) | DIO6 at CN4-5                                                                       | D14 socket (CN6-9) to the SDA row  | Zephyr label; readmes (2)                     |
| SDA row: PA7 (I2C3 SDA) | DIO8 at CN3-28                                                                      | A0 socket (CN7-1) to the SDA row   | Zephyr label; readmes (12)                    |
| 3V3 rail                | -                                                                                   | CN5-4 to the rail                  | Sidewalk SDK README                           |
| GND rail                | AD3 GND moves from CN5-6 to the rail                                                | CN5-6 to the rail                  | Sidewalk SDK README                           |
| Scopes (rise time)      | scope 1+ on a male pin in the SCL row, 2+ in the SDA row, 1- and 2- on the GND rail | -                                  | board file scope entries with `requires: i2c` |
| Pull-ups, EEPROM        | -                                                                                   | as on the NUCLEO-WB55RG            | [parts](#parts-for-the-i2c-option)            |

Without the option the standalone I2C tests run on `bundle1` with the MCU pull-ups (`pull=up`): I2C1 and I2C3 on PB2/PB1 (DIO5/DIO6) and I2C1 on PA15/PB3 (DIO2/DIO1).

#### NUCLEO-WBA55CG `--with spiloop` (`bundle2`)

SPI1 to SPI3; SPI3 is observed through DIO14, DIO10, DIO11 and DIO15. The PB8 end is the one the self-check drives: its LD3 load would spoil a pull-level read.

| Net               | AD3 leads                                                        | Jumper or breadboard end                                                                                        | Source                                             |
|-------------------|------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------|----------------------------------------------------|
| SCK: PB4 to PA0   | DIO0 at CN4-6; DIO14 at CN3-38                                   | D13 socket (CN6-6) to the A5 socket (CN7-6)                                                                     | Zephyr labels; readmes (12) and (9)                |
| MISO: PB3 to PB9  | DIO1 at CN4-10; DIO10 at CN4-23                                  | D12 socket (CN6-5) to the D7 socket (CN8-8)                                                                     | Zephyr labels; readmes (11) and (8)                |
| MOSI: PA15 to PB8 | DIO2 at CN4-15; DIO11 moved from CN4-38 to a male pin in the row | D11 socket (CN6-4) to a breadboard row; CN4-38 (PB8, its only position) to the same row with a male-female wire | Zephyr label; readmes (14) and (12)                |
| NSS: PA12 to PA5  | DIO3 at CN4-17; DIO15 at CN3-36                                  | D10 socket (CN6-3) to the A4 socket                                                                             | Zephyr labels; CN6-3 readmes (1); A4 by label only |

### Which tests skip, and how

- Load gate: a test that resolves an AD3 channel (`need.dio`, `need.wavegen`, `need.scope`, `optional_dio`, `optional_scope`) for a pin an enabled option loads skips with "pin X loaded by --with T", unless it is marked `uses_option("T")` or `requires_option("T")`. Pins an option ties to a channel with a jumper resolve only for such tests.
- Without `--with i2c`: the I2C target, EEPROM, Fast-mode, rise-time, data-NACK, RELOAD, clock-stretch, bus-error and general-call tests skip (`requires_option("i2c")`); default timing, 100 kHz, the address NACK and arbitration loss run on `bundle1` with `pull=up`.
- With `--with i2c`: on the NUCLEO-WB55RG the PWM tests of TIM1 CH2N (PB8), TIM1 CH3N and TIM17 CH1 (PB9), the GPIO loop tests on PB8/PB9 and every QUADSPI test that uses IO0/IO1 skip; on the NUCLEO-WBA55CG `bundle2` the tests of TIM1 CH1N/CH2N and USART2 CTS/RTS (PB2/PB1) and of TIM2 CH3/CH4 (PA7/PA6) skip.
- Without `--with spiloop`: the SPI2 (NUCLEO-WB55RG) instance tests and the SPI1 to SPI2/SPI3 loops skip. With it, tests on its loaded pins skip unless they use the option, and `--with loopback` cannot be enabled as well.
- With `--with loopback`: tests on PA6/PA7 (NUCLEO-WB55RG) or PA15/PB3 (NUCLEO-WBA55CG) skip unless marked `uses_option("loopback")`: QUADSPI, the SPI slave cases with the AD3 as master, PWM TIM1 CH1N and TIM16 CH1 (NUCLEO-WB55RG), TIM17 break and the encoder index (NUCLEO-WBA55CG).
- NUCLEO-WBA55CG `bundle2`: the ADC, LPUART1 and TIM1/TIM16 break tests skip (no DIO, wavegen or scope on their pins).
- `--with` a tag that no selected wiring set offers, or without `--wiring-set`: the run stops with a usage error.

## What is tested

Each peripheral has a feature file `host/tests/hil/features/<peripheral>.feature`, with one scenario per test, and a module `host/tests/hil/test_<peripheral>.py` that binds the scenarios to test functions and implements their steps.

- `test_wiring.py` - the bench wiring, with GPIO commands only and the AD3 outputs and pulls off: continuity of the jumpers of every enabled option, its external pull-ups, the jumpers of offered options that are not enabled (`pass --with <tag> or remove the wiring`), and that the pins of `tests.wiring.undriven` follow both MCU pulls. Run it first after wiring the board; it skips with `--fake`.
- `test_system.py` - `ping`, `info`, and the `board.pins` alias table against the board file in both directions, every alias accepted as a pin, reserved terminal/SWD/LSE/BOOT0 pins and the debug LED, unbonded pins, pin syntax, the terminal UART, error reasons (`usage`, `busy`, `notopen`, `range`, `unsupported`), missing instances (including 0), `delay`, `reset` and the `EVT boot` cause.
- `test_uid.py` - the 96-bit unique device ID of `info`: twelve bytes, not blank memory, the lot number in printable ASCII, the same after a reset.
- `test_clock.py` - `clock.info` against the board file: bus clocks, oscillator ready flags, the RNG kernel clock and (NUCLEO-WB55RG) the CLK48 source. On the NUCLEO-WB55RG also the clocks on the MCO pin (PA8, DIO0): SYSCLK/16 and HSI16/16 within 1 %, HSE/16 within 0.1 %, the LSE within 100 ppm (least-squares fit of the edge times), MCO off, and `clock.hsi48` stopping and restarting HSI48.
- `test_gpio.py` - output levels with every drive (`hal::Speed`), inputs following the AD3 with every pull, pull-only idle levels, open drain, the user LED, eight pins at a time, pins held by other groups, interrupt counts for edge x handler type x pulse count x frequency against exact AD3 pulse trains, one EXTI line per port at a time, `gpio.pulse` timing.
- `test_sgpio.py` - for `SynchronousOutputPinStm`: levels with every speed, the open-drain low level and the output latch, rebuilding on a changed `od` and the input left by `sgpio.release`.
  - For `SmallPeripheralPinStm` and `MultiGpioPinStm`/`MultiPeripheralPinStm`: a timer channel muxed to one pin, or to two or three pins at once, while `tpwm` runs that channel with no pin of its own (frequency and duty on every muxed pin); also the slots, pins shared with `gpio` and argument errors.
- `test_pwm.py` - for `PwmStm` and `SynchronousPwmStm`:
  - frequency x duty x edge/centre alignment x prescaler, with `ERR range` exactly where the period does not fit the counter (16 or 32 bits);
  - 1-4 channels, complementary outputs and complementary-only outputs with dead time and inversion (shoot-through check), the dead-time limit;
  - idle levels, the break input with both polarities and automatic re-enable;
  - frequency changes and stop, the features each timer offers (`unsupported` elsewhere), argument errors, one timer at a time, and that a re-open forgets dead time and break.
- `test_pwm_ext.py` - the PWM options of D.10: frequency, duty and alignment of every counter mode (rising edges shared counting up, falling edges counting down, pulse centres centre aligned), the one-tick pulse `edgedown` keeps at 0 %,
  compare preload (duty 80 % / 20 % written while the logic analyzer records: cut pulses only without preload), the break filter against AD3 pulses shorter and longer than it, `adc.open trgo=` paced by the PWM's trigger output (`update`, `oc1ref`), the timers that offer neither, and argument errors.
- `test_uart.py` - every baud rate x parity x driver variant (interrupt, DMA, duplex DMA, synchronous) on LPUART1 and USART2, both directions against the AD3 UART, frames decoded from the firmware TX
  line and the bit rate measured on it, large payloads, full-duplex streaming, RTS/CTS flow control per variant, send timeout with CTS held, re-open with other settings, TX/RX swap, the baud limits of
  each instance, argument errors and default pins.
- `test_uart_sendonly.py` - for `SynchronousUartStmSendOnly` (NUCLEO-WB55RG LPUART1 through the `SyncLpUart` constructors, NUCLEO-WBA55CG USART2): frames against the AD3 UART at several baud rates, no received data, `flow=rts` driving RTS while CTS is ignored, the last frame that a `uart.close` right after `uart.send` may cut, optional `rx`, default pins and argument errors.
- `test_spi.py` - SPI modes 0-3 x baud x driver variant (interrupt, DMA, synchronous) x GPIO chip select, decoded from the logic analyzer (MOSI, MISO, clock polarity and rate, chip-select release), continued sessions, receive-only first transfers, the largest transfer, the baud limits and argument errors, on SPI1 and on SPI2 (NUCLEO-WB55RG, `--with spiloop`) or SPI3 (NUCLEO-WBA55CG, `bundle2`).
- `test_spi_ext.py` - the SPI master options: the bit order (`lsb=1`) on every driver, frame sizes of 4 to 16 bits (`bits=`, `SpiDataSizeConfiguratorStm` on the DMA driver) decoded word by word, the 8/16-bit limit of the WBA55 SPI3, an 8-bit master reopened after a 16-bit one, the `nss`/`cs` checks, and the hardware slave select (a known gap).
- `test_spi_slave.py` - `SpiSlaveStmDma` clocked by the AD3 SPI master: full duplex, send-only and receive-only transfers of 1-1024 bytes at several clock rates (CRC beyond 128 bytes), clocks while nothing is armed, a master clocking more or less than armed, `spis.result` waiting and busy, cancel and re-open, argument errors and the instance shared with `spi`;
  with `--with spiloop` the board's own SPI master (every driver) against the slave on the other instance, both ways.
- `test_adc.py` - wavegen DC levels against raw 12-bit codes (checked against the scope when it is wired), sequences of up to 8 conversions, every sampling time, timer-triggered rates and the measure timeout, timer sharing with PWM and the encoder, unsupported trigger timers, argument errors and limits, and ADC pins against GPIO.
- `test_ain.py` - single conversions against wavegen levels and every listed sampling time, the internal temperature sensor in its room-temperature range, TIM2-paced bursts that give exactly n samples in n / rate (B.12), also twice on one driver, burst values and a wavegen ramp followed at its slope, the ADC, TIM2 and DMA channel shared with `adc` and `pwm`, argument errors.
- `test_dma.py` - `dma.wave`: 8- to 64-bit patterns at 1 kHz to 1 MHz decoded from the logic analyzer (32-bit DMA transfers to BSRR on both MCUs, B.4), TIM2 and the pin shared with `pwm` and `gpio`, argument errors.
- `test_qei.py` - position counts for frequency x cycles x direction x decoding x phase inversion, physically inverted phases restored by `inva`/`invb`, offset and rollover, speed, the index input through `qei.index`, default pins, the resolution limits, argument errors, one encoder at a time and timer sharing.
  - The LPTIM encoders with `cap=ab|rise|fall`: NUCLEO-WB55RG LPTIM1 in bundle2; NUCLEO-WBA55CG LPTIM2 in bundle1 and LPTIM1 in bundle2.
- `test_timer.py` - for `FreeRunningTimerStm` and `TimerWithInterruptStm` on every timer: the update rate on the marker pin (gpio0) with immediate and dispatched callbacks (dispatched at most 1 kHz), interrupt counts against the elapsed time, the free-running counter up and down (`ERR unsupported` down on TIM16/TIM17).
  - Also stop and restart, the marker pin claimed while open, argument errors, one timer at a time and the timer shared with pwm, tpwm, the encoder and the timer-triggered ADC.
- `test_timer_pwm.py` - for `TimerPwmWithChannels<N>`: frequency and duty 0/50/100 % of every wired channel (100 % keeps one low counter tick), a duty per channel, `SetPulse` changing the period of every channel, an unused `-` channel whose pin stays free, starting and stopping one channel or all, argument errors and the timer shared with pwm and tim (NUCLEO-WBA55CG TIM2 with CH3/CH4 in bundle2).
- `test_lptim.py` - for `FreeRunningLowPowerTimerStm` and `LowPowerTimerWithInterruptStm` on LPTIM1 and LPTIM2: the update rate on the marker pin (gpio0) with immediate and dispatched callbacks, every prescaler and the WBA55 repetition counter.
  - Also interrupt counts against the elapsed time, the free-running counter, stop, the kernel clock, argument errors and the LPTIM shared with the LPTIM encoder and `lptpwm`.
- `test_lptim_pwm.py` (NUCLEO-WBA55CG) - for `LpTimerPwmWithChannels<N>`: frequency and duty of LPTIM1 CH2 and LPTIM2 CH1/CH2, `SetPulse`, stop and restart, argument errors and one LPTIM PWM at a time. It asserts the requested duty; a bench that measures 1 - duty is recorded as a known gap (DESIGN R14).
- `test_watchdog.py` - the window watchdog with timeouts across its range x automatic or manual feeding (warnings, no reset while fed, `reset=wwdg` otherwise), the early-warning period measured on the `pin=` toggle, manual feeding, a single watchdog at a time, the timeout limits and argument errors.
- `test_i2c.py` - `I2cStm` on both instances.
  Without wiring, each instance on its own pins with the MCU pull-ups (`pull=up`): the default and computed TIMINGR (reply and bus timing against the Standard mode minima), address NACK and its recovery in both directions, the zero-length probe, arbitration loss with SDA held before `i2c.open` and pulled low by the AD3 inside the second address bit, and argument errors.
  With `--with i2c` (the master against the `i2cs` LL target on the other instance, 4.7 kOhm pull-ups): Standard and Fast mode timing and the rise time, data NACK at every position, the address NACK after the other direction, writes and reads of 1-1024 bytes across the NBYTES/RELOAD boundaries, repeated START, continued sessions in both directions,
  clock stretching, a bus error from a misplaced STOP, the general call, close while the bus is held and both instance directions.
- `test_eeprom.py` - EMIL's `eeprom.*` over the `I2cEepromStm` adapter on the external 24Cxx (`--with i2c`): the address probe, attach/detach, writes inside and across pages, reads across pages, write-read-write-read, erase, data across a reset, the size limit,
  ACK polling on the logic analyzer, recovery from an absent chip, detach while busy, and 1-byte word addresses against the `i2cs` register file.
- `test_rng.py` - for `SynchronousRandomDataGeneratorStm`, `RandomDataGeneratorStm` and (NUCLEO-WB55RG) `SynchronousSynchronizedRandomDataGeneratorStm`: exact lengths, two reads that differ, 64 KiB per variant through the monobit, byte chi-square and runs tests of `rngstats`, variants taking turns on the RNG.
  - On the NUCLEO-WB55RG the synchronized variant keeps HSI48 running when it was on and switches it off again when it was off, takes its locked branch with semaphore 5 held (`lock5=1`), and leaves HSEM semaphores 0 and 5 free (`hsem.status`, when that group is built in).
- `test_aes.py` - for `SynchronousAes128EcbStm`: the FIPS-197 C.1 and SP 800-38A F.1.1 known answers in both directions, block by block and four blocks at once, one to five blocks per command, a key change between commands, and every data swapping mode (`swap=none|half|byte|bit`) against the model of `crypto_ref`; argument errors.
- `test_pka.py` - for `PkaStm` on secp256r1: G, 2G and 3G, the default operands, a short scalar padded by the firmware, (n - 1)G = -G, the NIST CAVP ECC CDH vector, a given base point, points on and off the curve, comparisons of 4, 32 and 60-byte numbers, the duration of one multiplication and argument errors; every result against the P-256 arithmetic of `crypto_ref`.
- `test_qspi.py` (NUCLEO-WB55RG) - for `QuadSpiStm` (`variant=poll`), `QuadSpiStmDma` (`variant=dma`) and `SingleSpeedQuadSpiStmDma` (`variant=spi`): instruction, address, alternate bytes and data of writes decoded on the logic analyzer on 1 and on 4 lines, and SPI mode 0 transfers.
  - Reads: 4-line reads of static AD3 levels, a status poll that matches and one that times out (`variant=dma`; `variant=poll` hangs, a known gap).
  - Also the B.15 fix: writes answer the FIFO level of their completion callback (`flevel=0`) and two writes issued from one completion callback both reach the bus; the clock, the CRC of 256-byte reads, argument errors and the pins held while open.
  - The AD3 never drives a line the QUADSPI drives: it decodes writes and holds only lines the QUADSPI receives on, with its weakest drive. Every case skips while `--with loopback`, `--with spiloop` or `--with i2c` loads a QUADSPI pin.
- `test_backup_ram.py` - `hal::BackupRamStm` as `hal::BackupRam<volatile uint32_t>` (RTC BKP0R-BKP19R on the NUCLEO-WB55RG, TAMP BKP0R-BKP15R on the NUCLEO-WBA55CG): the word count, every word written and read back with four patterns, fill and check, the words kept through a reset, argument errors.
- `test_flash.py` - the internal flash drivers (`FlashHomogeneousInternalStm`, `FlashInternalStm`, their synchronous twins and, on the NUCLEO-WB55RG, `FlashCoordinatedWithWirelessStack`) over a scratch region of 80 pages (NUCLEO-WB55RG) or 64 pages (NUCLEO-WBA55CG), with one sector per page and with a sector-size table of 1-, 2- and 4-page sectors:
  - the geometry and the first sector the firmware lets the tests erase (an erase that took the sector index for the absolute page could not reach the running image from there on);
  - every variant and layout erases exactly the pages of the sector it is given (the marker in the next sector stays, the previous sector is unchanged);
  - aligned, unaligned, odd-length, page-crossing and generated 300-byte writes on distinct flash words, read back exactly through every variant, with the bytes around them still erased; a programmed word refused with `ERR failed`; the page erase time;
  - NUCLEO-WB55RG: a coordinated write and erase wait for HSEM 7 while it is held; `flash.stack starting` holds a write (its `ERR timeout` goes out, the flash stays erased) until `flash.stack fus` completes it with `EVT flash`;
  - NUCLEO-WB55RG: the coordinated driver borrows the watchdog (`wdt.start` busy) and excludes `hsem.lock`, and a coordinated erase under a running watchdog does not reset the board.
- `test_hsem.py` - NUCLEO-WB55RG hardware semaphores:
  - two-step take and release per process with the lock state read from R, another process refused, `hold=` releasing on time;
  - `SynchronousHardwareSemaphoreStm` waiting until the scaffold timer frees a semaphore held by process 1 (and returning at once on a free one), a held semaphore refused instead of a lock that would block for good;
  - `IsLockedByCurrentCore` as a query that takes no lock, argument errors.
- `test_low_power.py` - `hal::LowPowerModeStm` in `sleep` and `deep` (Sleep on these MCUs: the clock-restore callback is never called):
  - with only the wake line and TIM17 enabled, the marker pin stays low until the AD3 edge on the wake pin (both edges) and rises within 50 us of it, and `us` covers the time asleep;
  - without an edge the TIM17 timeout ends the window and the tick runs again; pins released; argument errors.
- `test_unsupported.py` - every comparator, CAN and Ethernet command, and the commands of the groups the running MCU lacks (PROTOCOL.md, "Not available on these boards"), answer `ERR unsupported`.

## Customising

Each board file (`host/boards/<board>.yaml`) holds:

- `terminal`, `clocks` (the kernel clocks the expectations use), `pins` (the PROTOCOL.md alias table, compared with `board.pins`; only the generic names of `hal_st_validation.protocol` are accepted) and `ad3` (supplies, analog limits).
- `wiring_sets` - per set, `dio`, `wavegen` and `scope` maps from AD3 channel to pin or alias. An entry is either a pin or a mapping with `pin`, `jumpered` (pins tied to `pin` with a wire), `role` (a name tests can look up), `note` and `requires` (only used with `--with <tag>`, which the set must offer); `jumpers` documents extra wiring.
- `wiring_sets.<set>.options.<tag>` - the optional wiring a set offers: a description, or a mapping with `description`, `jumpered` (key pin -> pins the wiring ties to it; the self-check drives the key), `loads` (pins the wiring loads), `pullups` (pins it pulls up to 3V3) and `excludes` (options that cannot be fitted at the same time).
- `known_gaps` - driver gaps with the test ids (wildcards `*` and `?`) they affect, the reason and whether they hang the firmware.
- `tests` - the parameters of every test module: parameter matrices, pins and instances, levels and tolerances.
  - `@pytest.mark.matrix("pwm.waveform")` turns every key of that mapping into one test parameter of the same name; `@pytest.mark.board_params("argname", "section.key")` adds one parameter from a list. These markers go on the function a scenario is bound to, which takes the parameters as arguments; the steps read them as fixtures.
  - All parameters of a test form one matrix: `--depth full` runs its product, `--depth quick` a pairwise subset (`hal_st_validation.pairwise`); `@pytest.mark.constraint(valid=...)` removes combinations a driver cannot take (for example parity with the synchronous UART).
  - Extending a sweep or moving a peripheral to other pins is a YAML change.
  - `tests.wiring.undriven` lists pins nothing on the board may drive (solder bridges to ST-LINK lines); `test_wiring.py` checks them.
  - `@pytest.mark.wiring_options("tag")` runs a test once per enabled option (`enabled=False`: per offered option that is not enabled), each case marked `uses_option`.

The other markers are tags in the feature files: `@ad3` (also opens the AD3 for the scenario and resets its outputs afterwards), `@slow`, `@resets_board`, `@family:stm32wb55`, `@requires_option:i2c`, `@uses_option:spiloop` and `@conflicts_option:i2c`. A tag on the `Feature` line applies to all of its scenarios.
To add a test, add a scenario to the feature file, bind it with `@scenario("<peripheral>.feature", "<scenario name>")` in the module and write the steps it is missing. `pytest tests/unit/test_features.py` fails while a scenario is not bound or a step has no definition. Without hardware most scenarios skip before their first step, so pytest-bdd would not report these until a run on the bench.

To validate another board, add a board profile under `firmware/boards/<mcu>/` and the MCU to `emil_build_for` in `firmware/CMakeLists.txt`, copy a board file, adapt the pins, wiring sets and parameters, and pass `--board path/to/board.yaml`.

## Interactive console

```bash
hal-st-console --port /dev/ttyACM0
hal-st-console --port /dev/ttyACM0 -c info -c board.pins
```

`hal-st-console` is `ad3-bench-console` from ad3-waveforms-bench with the firmware's 921600 baud and its own history file (`ad3-bench-console --port /dev/ttyACM0` works as well).
The console forwards commands, prints final lines and events, and keeps a history in `~/.hal_st_validation_history`. `:wait <s>` listens for events, `:raw` also shows non-protocol output, `:quit` leaves.

## Package layout

In `tests`:

- `hil/features/*.feature` - the scenarios, one feature per peripheral.
- `hil/test_*.py` - the scenario bindings with their parametrisation markers, the step definitions and the helpers of one peripheral.
- `conftest.py` - the command line options, the board-file parametrisation, `known_gaps`, the tag hook and the fixtures (`fw`, `need`, `board_cfg`, `ad3_released`, the per-test cleanup).
- `unit/` - tests of `hal_st_validation` and of the scenario bindings that need no hardware.

In `hal_st_validation` (hal-st specific):

- `firmware.py` - typed API with one group per PROTOCOL.md section (`fw.system`, `fw.gpio`, `fw.pwm`, `fw.uart`, `fw.spi`, `fw.adc`, `fw.qei`, `fw.wdt`); keyword arguments map 1:1 to protocol options (`continue_` for `continue`). The groups of `groups/*.py` are attached under the names their `GROUPS` mapping exports.
- `groups/` - `Group` (`groups/base.py`: `_cmd`, `begin` for commands whose final line comes later, pin resolution) and one module per area exporting `GROUPS = {"name": GroupClass}`.
- `fakes/` - `FakeGroup` and the argument helpers (`fakes/base.py`); one module per area with the fake firmware's model of its command groups, found automatically.
- `patterns.py` - the payloads the firmware generates (`len=`, `pattern=inc|const|prbs`, `seed=`) and their CRC-32 (`crc=`).
- `i2c.py` - I2C bus helpers: decoding SCL/SDA captures (`i2c_decode`), SCL timing and rise times, `expected_timing` (the host twin of the firmware's TIMINGR computation), and AD3 captures and SDA pulses started by a START condition; `groups/i2c.py` - the `fw.i2c`, `fw.i2cs` and `fw.eeprom` wrappers; `fakes/i2c.py` - their fake groups with one shared bus, the target and a 24LC256.
- `spiwords.py` - SPI words of 4-16 bits: a logic-capture decoder without the bench decoder's 8-bit mask (`spi_decode_words`) and the frame model of `SpiMasterStmDma` with a data size configurator (`spi_frames`, `spi_bytes`); `groups/spis.py` - the `fw.spis` wrapper; `fakes/spis.py` - its fake group and the SPI loop of the fake `spi.xfer` (`FakeFirmware.spi_loop`).
- `groups/timers.py` - `fw.tim`, `fw.tpwm`, `fw.lptim`, `fw.lptpwm` and their expected waveforms (`timer_update_rate`, `lptim_update_rate`, `pwm_duty_fraction`); `fakes/timers.py` is their fake firmware model.
- `groups/analog.py` - `fw.ain` (`read`, `burst` with one `BurstRun` per measurement) and `fw.dma` (`wave`), with the expectations of the TIM2 pacing (`trigger_timing`, `trigger_rate`, `burst_seconds`) and the wave bit order (`wave_bits`); `fakes/analog.py` models both groups and their deferred replies.
- `groups/io.py` - `fw.sgpio`, `fw.clock` (`ClockInfo`; `clock.mco` and `clock.hsi48` register their undo with `close_all`), `edge_fit_frequency` and `parse_uid`; `fakes/io.py` is their fake firmware model, with a unique device ID of the real layout.
- `crypto_ref.py` - pure-Python AES-128 with the data swapping model of the STM32 AES peripheral, and affine P-256 arithmetic, with the FIPS-197, SP 800-38A and CAVP ECC CDH vectors; `rngstats.py` - the `rng.stats` counts and the monobit, chi-square and runs bounds.
- `groups/crypto.py` - `fw.rng`, `fw.aes`, `fw.pka`; `fakes/crypto.py` is their fake firmware model (a deterministic RNG, AES and PKA through `crypto_ref`).
- `groups/system_ext.py` - `fw.flash`, `fw.hsem`, `fw.bkp`, `fw.lpm` (`flash.stack starting` and `hsem.take` without `hold` register their undo with `close_all`), the sector table of `layout=table`, `flash_words` and `bkp_fill_value`.
- `fakes/system_ext.py` - their fake firmware model: a flash array with page erase, HSEM state on the fake clock, backup words that survive `reset`, an `lpm.enter` that wakes at once, and the `wdt.start` refusal while the coordinated flash borrows the watchdog.
- `groups/qspi.py` - `fw.qspi` and the decoding of QUADSPI captures (`qspi_samples`, `qspi_bytes`, `qspi_frame`); `fakes/qspi.py` is its fake firmware model (STM32WB55).
- `protocol.py` - the hal-st part of the protocol: error reasons, `P<port><index>` pins (ports A-K, index 0-15) and the generic alias names (`normalize_pin`, `parse_pin_map`).
- `config.py` - board file loading, wiring-set merging, parameter matrices, overrides and known gaps; `expect.py` - expected STM32 values (PWM quantisation and range, SPI prescaler, UART baud-rate register limits, WWDG prescaler and period, ADC codes, encoder counts).
- `pairwise.py` - the full product and the deterministic pairwise generator behind `--depth`.
- `fake_firmware.py` - `FakeFirmware`, an in-memory stand-in for the validation firmware used by the unit tests and `--fake`.
- `console.py` - the `hal-st-console` entry point.

In the firmware (`firmware/`):

- `Main.cpp` composes the console, the pin pool and every command group; `Console` is the terminal on USART1; `boards/<mcu>/BoardProfile.hpp` holds the aliases, reserved pins, default pins, DMA request lines, clocks and ADC tables of each board.
- `PinFactoryStm` builds `hal::GpioPinStm`s over the generated pinout tables (bonded pins only, analog sharing, one EXTI line per port); `BoardInfoStm` reports the board, clock and reset cause; `TimerAllocation` keeps PWM, encoder and timer-triggered ADC off each other's timer.
- `ResourceAllocation` keeps the groups that share an I2C, SPI, ADC or LPTIM instance, a DMA channel or an HSEM semaphore off each other, with the owner ids of `Owners.hpp`; `Payload` generates the `len=`/`pattern=` payloads and the `out=crc` CRCs; `ChannelPins`, `Stopwatch` (microseconds from the cycle counter) and `HsemMaster` (the one HSEM master of the STM32WB55) serve the newer groups.
- One factory per command group (`UartFactory`, `SpiFactory`, `AdcFactory`, `PwmFactory`, `QeiFactory` with the `qei.index` command, `WatchDogFactory`) parses the hal-st options and builds the driver; `UnsupportedGroups` answers the rest.
  `AnalogInputGroup` (`ain.read`, `ain.burst`) and `DmaGroup` (`dma.wave`) build their drivers per command and claim the ADC, TIM2 and the DMA channels through `TimerAllocation` and `ResourceAllocation`.
- `I2cGroup` (`i2c.*` over `I2cStmForHil`, an `I2cStm` whose hooks report `EVT i2c`), `I2cTiming` (TIMINGR from kernel clock and bus frequency), `I2cTarget` (`i2cs.*`, the LL I2C target scaffold), `I2cEepromStm` (a `hal::Eeprom` over `hal::I2cMaster` with page writes and ACK polling) and `EepromGroup` (`eeprom.attach`/`eeprom.detach`, the factory of EMIL's `eeprom` group).
- `SpiSlaveGroup` (`spis.*`: `SpiSlaveFactory` builds `SpiSlaveStmDma` on the board's slave DMA channels, `SpiSlaveCommands` arms one transfer at a time and answers `spis.result` from its own timer); `SpiFactory` also takes `bits`, `lsb` and `nss` and holds the SPI instance in `ResourceAllocation`.
- `TimerGroup`, `TimerPwmGroup`, `LpTimerGroup` and `LpTimerPwmGroup` serve `tim.*`, `tpwm.*`, `lptim.*` and `lptpwm.*`, each a factory and a single-instance group; `QeiFactory` also builds the LPTIM encoder with `cap=rise|fall` and holds its LPTIM in `ResourceAllocation`.
- `SyncGpioGroup` serves `sgpio.*` through `SyncGpioDriver`, the only translation unit that includes `SynchronousGpioStm.hpp`; `ClockGroup` serves `clock.*`; `UartFactory` also builds `SynchronousUartStmSendOnly` (`sendonly=1`).
- `RngGroup`, `AesGroup` and `PkaGroup` are stateless command groups: `rng` and `aes` build their driver per command; `pka` builds one `PkaStm` on first use and keeps it.
- `FlashGroup`, `HsemGroup` (STM32WB), `BackupRamGroup` and `LowPowerGroup` serve `flash.*`, `hsem.*`, `bkp.*` and `lpm.*`; `WatchDogFactory` lends the WWDG to the coordinated flash driver (`Borrow`/`Return`).
