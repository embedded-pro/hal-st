# STM32F429I-DISC1 example

Exercises the on-board peripherals of the STM32F429I-DISC1 (MB1075, STM32F429ZIT6) through hal-st and EMIL drivers. Trace goes over SEGGER RTT, so it works on every board revision and needs no pins.

Build and flash:

```bash
cmake --preset stm32f429
cmake --build --preset stm32f429-RelWithDebInfo --target examples_st.stm32f429i_disco
```

The `stm32f429` preset sets `EMIL_INCLUDE_SEGGER_RTT`; without it the example is not generated.

## What it does

| Part                             | Pins                                                            | Driver                                                                                     | Behaviour                                                                        |
|----------------------------------|-----------------------------------------------------------------|--------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------|
| LEDs                             | LD3 green PG13, LD4 red PG14                                    | `hal::GpioPinStm`, `services::DebugLed`                                                    | LD3 heartbeat, LD4 on while the display reports an underrun                      |
| User button                      | PA0                                                             | EMIL `services::DebouncedButton`                                                           | prints the gyroscope values and starts a die temperature measurement            |
| ILI9341 QVGA panel               | LTDC RGB666 pins, SPI5 for set-up, CS PC2, D/CX PD13           | `hal::LtdcStm`, `hal::Dma2dStm`, EMIL `boards::Stm32f429iDiscoLcdSetup`                    | the shared `display_demo`: double-buffered RGB565 window plus an ARGB overlay    |
| 64 Mbit SDRAM                    | FMC bank 2, 16-bit                                              | `hal::SdRamStm`                                                                            | holds the frame buffers; the last 64 KB are tested at boot                       |
| I3G4250D / L3GD20 gyroscope      | SPI5 PF7/PF8/PF9, CS PC1                                        | `hal::SpiMasterStm`, `services::SpiMultipleAccess`, EMIL `drivers::L3gd20Core`             | polled at 100 Hz, angular rate in mdps every second                              |
| STMPE811 touch controller        | I2C3 PA8/PC9, INT PA15                                          | `hal::I2cStm`, EMIL `drivers::Stmpe811`                                                    | prints pressed, moved and released with the raw 12-bit coordinates               |
| ADC, internal temperature sensor | internal                                                        | `hal::AnalogToDigitalInternalTemperatureStm`                                               | die temperature every 2 s from the factory calibration                           |
| DAC                              | PA5 (DAC_OUT2)                                                  | `hal::DacStm`, `hal::DigitalToAnalogPinImplStm`                                            | steps through five levels, one per second; probe PA5 with a meter or a scope     |
| RNG, unique ID, backup registers |                                                                 | `hal::RandomDataGeneratorStm`, `hal::UniqueDeviceId`, `hal::BackupRamStm`                  | boot report with a boot counter                                                  |

The LCD and the gyroscope share SPI5, so both go through one `services::SpiMultipleAccessMaster`; each device has its own `services::SpiMultipleAccess` and chip select.

The gyroscope is an I3G4250D on MB1075 revision E and later and an L3GD20 before that. Both use the register map of `drivers::L3gd20Core` and differ only in WHO_AM_I, so the example tries 0xD3 and then 0xD4 and traces the one that answers.

The temperature uses the factory calibration values (30 °C and 110 °C), which ST takes at a VDDA of 3.3 V; another supply voltage shifts the reading.

Not included, because the board or the libraries do not allow it:

- USB OTG: the connector sits on OTG_HS (PB12 to PB15) in full-speed mode, `hal::UsbDeviceLinkLayerStm` only drives OTG_FS, and hal-st has no device class stack.
- USART1: the STM32F429I-DISC1 (ST-LINK/V2-B) can route its virtual COM port to USART1 (PA9/PA10), the original 32F429IDISCOVERY (ST-LINK/V2) cannot. RTT works on both and leaves USART1 free.
- CAN, Ethernet, DCMI, SDIO: the board has no transceiver, PHY, camera or card socket, and the pins they need on the extension headers are taken by the LCD, the SDRAM or the USB connector.
- PWM: PG13 and PG14 have no timer output, so the LEDs cannot be dimmed.
- Gyroscope interrupt lines: the example polls the status register instead.
- ADC on a pin: nothing on the board drives an ADC input, and hal-st reserves a pin once, so PA5 cannot be both the DAC output and an ADC input to read the DAC level back.

## Viewing the RTT trace

Find the control block in the map file (`_SEGGER_RTT`), flash, then attach a host. The RAM region of the `stm32f429` linker script is 128 KB at 0x20000000.

OpenOCD:

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "init" -c "rtt setup 0x20000000 0x20000 \"SEGGER RTT\"" -c "rtt start" -c "rtt server start 9090 0"
nc localhost 9090
```

probe-rs finds the control block by itself:

```bash
probe-rs run --chip STM32F429ZITx build/stm32f429/examples/stm32f429i_disco/RelWithDebInfo/examples_st.stm32f429i_disco.elf
```

The boot counter lives in the RTC backup registers, so it survives a reset but not a power cycle.

Output only reaches the host while a host is attached: the up buffer is small and the writer drops what does not fit, so touch tracing without a host drops the later lines.

## Not verified on hardware

Nothing here has run on a board yet. Things to check first:

- The boot report: the SDRAM test must print 0 errors, and the gyroscope and the STMPE811 must both be found.
- Pin assignments not checked against the MB1075 schematic: PC1 (gyroscope chip select), PA15 (touch interrupt, also used by Zephyr's board file) and PA5 (assumed free for the DAC).
- The touch coordinates are the raw controller values; their orientation and range are not calibrated to the 240 x 320 panel.
- The panel set-up and the gyroscope start up at the same time on SPI5; if either never answers, look at the arbitration in `services::SpiMultipleAccess` first.
