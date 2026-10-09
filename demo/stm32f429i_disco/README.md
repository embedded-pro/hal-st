# STM32F429I-DISC1 example

Exercises the on-board peripherals of the STM32F429I-DISC1 (MB1075, STM32F429ZIT6) through hal-st and EMIL drivers. Trace output and a command terminal share USART1.

Build and flash:

```bash
cmake --preset stm32f429
cmake --build --preset stm32f429-RelWithDebInfo --target demo_st.stm32f429i_disco
```

## What it does

| Part                             | Pins                                                 | Driver                                                                            | Behaviour                                                                     |
|----------------------------------|------------------------------------------------------|-----------------------------------------------------------------------------------|-------------------------------------------------------------------------------|
| USART1 terminal                  | PA9 (TX), PA10 (RX), 115200 8N1                      | `hal::UartStmDma` (DMA2 stream 7), EMIL `services::TerminalWithStorage`           | trace output and the commands below                                           |
| LEDs                             | LD3 green PG13, LD4 red PG14                         | `hal::GpioPinStm`, `services::DebugLed`                                           | LD3 heartbeat, LD4 on while the display reports an underrun                   |
| User button                      | PA0                                                  | EMIL `services::DebouncedButton`                                                  | prints the gyroscope values and the die temperature                           |
| ILI9341 QVGA panel               | LTDC RGB666 pins, SPI5 for set-up, CS PC2, D/CX PD13 | `hal::LtdcStm`, `hal::Dma2dStm`, EMIL `boards::Stm32f429iDiscoLcdSetup`           | the shared `display_demo`: double-buffered RGB565 window plus an ARGB overlay |
| 64 Mbit SDRAM                    | FMC bank 2, 16-bit                                   | `hal::SdRamStm`                                                                   | holds the frame buffers; the last 64 KB are tested at boot                    |
| I3G4250D / L3GD20 gyroscope      | SPI5 PF7/PF8/PF9, CS PC1                             | `hal::SpiMasterStmDma`, `services::SpiMultipleAccess`, EMIL `drivers::L3gd20Core` | sampled at 100 Hz, `gyro` shows the latest angular rate in mdps               |
| STMPE811 touch controller        | I2C3 PA8/PC9, INT PA15                               | `hal::I2cStm`, EMIL `drivers::Stmpe811`                                           | prints pressed, moved and released with the raw 12-bit coordinates            |
| ADC, internal temperature sensor | internal                                             | `hal::AnalogToDigitalInternalTemperatureStm`                                      | `temp` shows the die temperature from the factory calibration                 |
| DAC to ADC loopback              | DAC PA5 (DAC_OUT2) wired to ADC PC3 (ADC1_IN13)      | `hal::DacStm`, `hal::AdcStm`, `hal::AnalogToDigitalPinImplStm`                    | `dac`, `adc` and `loopback` set the DAC and read it back                      |
| RNG, unique ID, backup registers |                                                      | `hal::RandomDataGeneratorStm`, `hal::UniqueDeviceId`, `hal::BackupRamStm`         | boot report with a boot counter                                               |

The LCD and the gyroscope share SPI5, so both go through one `services::SpiMultipleAccessMaster`; each device has its own `services::SpiMultipleAccess` and chip select.
SPI5 uses the DMA master (DMA2 streams 3 and 4): `hal::SpiMasterStm` ends the session before it delivers its done callback, and `SpiMultipleAccess` would start the next client's transfer in between.

The gyroscope is an I3G4250D on MB1075 revision E and later and an L3GD20 before that. Both use the register map of `drivers::L3gd20Core` and differ only in WHO_AM_I, so the example tries 0xD3 and then 0xD4 and traces the one that answers.

The temperature uses the factory calibration values (30 °C and 110 °C), which ST takes at a VDDA of 3.3 V; another supply voltage shifts the reading, for example about 30 °C too high at 3.0 V. The reply prints the raw count and both calibration counts so the reading can be checked.

## Terminal

The ST-LINK/V2-B of the STM32F429I-DISC1 routes its virtual COM port to USART1, so the terminal appears as a serial port on the host (`/dev/ttyACM0`, `COMx`) at 115200 8N1. The original 32F429IDISCOVERY has a plain ST-LINK/V2 without a virtual COM port; connect a USB-serial adapter to PA9 (board TX), PA10 (board RX) and ground instead.

`help` lists the commands; each one has a short alias.

| Command        | Alias  | Description                                              |
|----------------|--------|----------------------------------------------------------|
| `gyroscope`    | `gyro` | Show the latest angular rate                             |
| `temperature`  | `temp` | Measure the die temperature                              |
| `adc`          | `a`    | Measure the ADC input on PC3                             |
| `dac <0-4095>` | `d`    | Set the DAC output on PA5 and read it back on PC3        |
| `loopback`     | `loop` | Sweep the DAC over five levels and read each back on PC3 |
| `sdram`        | `sd`   | Repeat the SDRAM test                                    |

Touch events and the boot report are printed as they happen. The boot report is written before the event dispatcher starts, and the 2 KB transmit buffer holds all of it and the `help` table.

The boot counter lives in the RTC backup registers, so it survives a reset but not a power cycle.

## DAC to ADC loopback

The F4 cannot route the DAC to an ADC inside the chip, and hal-st claims a pin only once, so PA5 and PC3 are separate pins joined by a jumper wire.
With the wire fitted, `dac 2048` should read back close to 2048 and `loopback` prints one line per level.

The DAC output buffer cannot reach the rails, so the sweep runs from 512 to 3584 and avoids 0 and 4095.
The difference printed is the ADC reading minus the DAC code; a large one means the wire is missing or PC3 is loaded by something on the board.

## Not included

The board or the libraries do not allow these:

- USB OTG: the connector sits on OTG_HS (PB12 to PB15) in full-speed mode, `hal::UsbDeviceLinkLayerStm` only drives OTG_FS, and hal-st has no device class stack.
- CAN, Ethernet, DCMI, SDIO: the board has no transceiver, PHY, camera or card socket, and the pins they need on the extension headers are taken by the LCD, the SDRAM or the USB connector.
- PWM: PG13 and PG14 have no timer output, so the LEDs cannot be dimmed.
- Gyroscope interrupt lines: the example polls the status register instead.

## Checked on hardware

Run on an STM32F429I-DISC1 with an L3GD20 (WHO_AM_I 0xD4) through the ST-LINK virtual COM port: the boot report prints in full (SDRAM test 0 errors, STMPE811 and gyroscope found), every terminal command replies, `help` prints the whole table and LD3 toggles.

## Not verified on hardware

- The LCD pattern and the touch events: nobody has looked at the panel or touched it yet.
- The DAC to ADC loopback: `adc`, `dac` and `loopback` read nonsense until the PA5 to PC3 wire is fitted.
- The temperature: it reads 87.5 °C (raw 1115, calibration 918 at 30 °C and 1192 at 110 °C) on a board at room temperature. The conversion matches the printed counts and the sample time is 480 cycles, so the suspects are a VDDA below 3.3 V, which the calibration assumes, and a die warmed by the LCD, SDRAM and DMA traffic. Measure VDDA with a multimeter.
- Pin assignments not checked against the MB1075 schematic: PC1 (gyroscope chip select), PA15 (touch interrupt, also used by Zephyr's board file), PA5 (assumed free for the DAC) and PC3 (assumed free for the ADC).
- The touch coordinates are the raw controller values; their orientation and range are not calibrated to the 240 x 320 panel.
