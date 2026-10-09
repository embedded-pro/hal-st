# STM32F4DISCOVERY example

Exercises the on-board peripherals of the STM32F4DISCOVERY (MB997, STM32F407VGT6) through hal-st and EMIL drivers. Trace goes over SEGGER RTT, because the ST-LINK/V2 on this board has no virtual COM port.

Build and flash:

```bash
cmake --preset stm32f407
cmake --build --preset stm32f407-RelWithDebInfo --target demo_st.stm32f407g_disco
```

The `stm32f407` preset sets `EMIL_INCLUDE_SEGGER_RTT`; without it the example is not generated.

## What it does

| Part                             | Pins                                                      | Driver                                                                    | Behaviour                                                         |
|----------------------------------|-----------------------------------------------------------|---------------------------------------------------------------------------|-------------------------------------------------------------------|
| LEDs                             | PD12 green, PD13 orange, PD14 red, PD15 blue (TIM4 CH1-4) | `hal::PwmStm`                                                             | brightness follows the tilt: +Y green, -X orange, -Y red, +X blue |
| LIS3DSH accelerometer            | SPI1 PA5/PA6/PA7, CS PE3                                  | `hal::SpiMasterStmDma` + EMIL `drivers::Lis3dshCore`                      | polled at 100 Hz; INT1/INT2 are unused                            |
| User button                      | PA0                                                       | EMIL `services::DebouncedButton`                                          | starts and stops the tone                                         |
| CS43L22 DAC                      | I2C1 PB6/PB9, I2S3 PC10/PC12/PA4, MCLK PC7, reset PD4     | `hal::I2cStm`, `hal::I2sOutputStm`, EMIL `drivers::Cs43l22`               | 440 Hz tone on the headphone jack                                 |
| MP45DT02 microphone              | I2S2 PB10 (clock), PC3 (data)                             | `hal::I2sInputStm` (PDM), EMIL `drivers::Mp45dt02`, `PdmDecimator`        | RMS and peak level every 500 ms                                   |
| ADC                              | internal                                                  | `hal::AnalogToDigitalInternalTemperatureStm`                              | die temperature every 2 s                                         |
| RNG, unique ID, backup registers |                                                           | `hal::RandomDataGeneratorStm`, `hal::UniqueDeviceId`, `hal::BackupRamStm` | boot report with a boot counter                                   |

`PdmDecimator` is the `drivers::PdmToPcm` for the microphone: a fourth-order CIC filter (64:1, 1.024 MHz to 16 kHz), a three-tap compensation filter and a DC blocker.

PLLI2S is set once to 86 MHz in `Main.cpp` and shared by I2S2 and I2S3, so the codec runs at 48 kHz (MCLK = 256 x fs) and the microphone clock at about 1.024 MHz.

Not included, because the board or the libraries do not allow it:

- USB OTG FS: hal-st only has the link layer and no device class stack.
- DAC: PA4 and PA5 are taken by I2S3_WS and SPI1_SCK.
- CAN, Ethernet, DCMI, FSMC: the board has no transceiver, PHY, camera or memory.
- UART trace: no virtual COM port on this ST-LINK.
- INT1 (PE0): shares EXTI line 0 with the user button (PA0), and `hal::GpioStm` allows one handler per line.

## Viewing the RTT trace

Find the control block in the map file (`_SEGGER_RTT`), flash, then attach a host.

OpenOCD:

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "init" -c "rtt setup 0x20000000 0x20000 \"SEGGER RTT\"" -c "rtt start" -c "rtt server start 9090 0"
nc localhost 9090
```

probe-rs finds the control block by itself:

```bash
probe-rs run --chip STM32F407VGTx build/stm32f407/demo/stm32f407g_disco/RelWithDebInfo/demo_st.stm32f407g_disco.elf
```

The boot counter lives in the RTC backup registers, so it survives a reset but not a power cycle.

## Not verified on hardware

Nothing here has run on a board yet. Things to check first:

- The PLLI2S values give the codec and the microphone a rate within 1 %; the I2S drivers assert otherwise.
- The microphone data is valid on the clock edge that `pdmSampleEdge` selects (rising by default). If the level never leaves the noise floor, try `falling` in `Main.cpp`.
- The LED mapping depends on how the board is held; swap the arguments of `pwm.Start` in `TiltLeds::Update` to taste.
- Boards older than revision C carry a LIS302DL instead of the LIS3DSH; EMIL has `drivers.imu.lis302dl` for them.
