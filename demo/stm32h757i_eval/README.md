# STM32H757I-EVAL demo

Exercises the on-board peripherals of the STM32H757I-EVAL (MB1246, STM32H757XIH6) through hal-st and EMIL drivers. Trace output and a command terminal share USART1 (PB14 TX, PB15 RX, 115200 8N1; RS-232 on CN2 or the ST-LINK virtual COM port, selected by JP7 and JP8).

The Cortex-M7 image runs the demo; the Cortex-M4 image only blinks the orange LED. Flash both. When the option bytes leave the Cortex-M4 boot disabled (BCM4 cleared, as on some boards), `hal::WaitForCortexM4Stop()` sets `RCC_GCR.BOOT_C2` so the Cortex-M4 still starts.

```bash
cmake --preset stm32h757-cm7
cmake --build --preset stm32h757-cm7-RelWithDebInfo --target demo_st.stm32h757i_eval_cm7
cmake --preset stm32h757-cm4
cmake --build --preset stm32h757-cm4-RelWithDebInfo --target demo_st.stm32h757i_eval_cm4
```

## What it does

| Part                     | Pins                                                                 | Driver                                                                                     | Behaviour                                                                                                                                                                                             |
|--------------------------|----------------------------------------------------------------------|--------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 4.3" 800 x 480 DSI panel | DSI lanes, reset PF10, backlight PA6                                 | `hal::DsiHostStm`, `hal::LtdcStm`, `hal::Dma2dStm`, EMIL `boards::Mb1166Setup`             | RGB565 dashboard in SDRAM; DMA2D clears it, the CPU draws the text, bars and tiles                                                                                                                    |
| FT6x06 touch controller  | I2C1 PB6/PB7, address 0x38                                           | `hal::I2cStm`, EMIL `drivers::Ft6x06` (`hal::TouchScreen`)                                 | probed from 0.5 s after the panel is ready, then polled at 50 Hz; a cursor on the second LTDC layer follows the finger and the pad on the right is drawn on                                           |
| Wake-up button           | PA0                                                                  | `services::DebouncedButton`                                                                | toggles the audio mute                                                                                                                                                                                |
| Tamper (USER) button     | PC13, active low                                                     | `services::DebouncedButton`                                                                | repeats the SDRAM test                                                                                                                                                                                |
| Joystick, microSD detect | MFX GPIO0 to GPIO4 (select, down, left, right, up), GPIO15           | `main_::Mfx` over I2C1, address 0x42                                                       | shown on the dashboard; select clears the touch pad; the SD detect level is traced                                                                                                                    |
| microSD                  | CK PC12, CMD PD2, D0-D3 PC8-PC11; level shifter PC6, PC7, PB9        | `hal::SdCardStm`, EMIL `hal::BlockDevice`, `examples::SdCardDemo`                          | boot: capacity on the dashboard or `NO CARD`, nothing is written; `sdcard` runs the validation test on the last 16 blocks and shows the result                                                        |
| 256 Mbit SDRAM           | FMC bank 2 (SDNE1, SDCKE1), 32-bit, 0xD0000000                       | `hal::SdRamStm`                                                                            | frame buffer in the first 768 KB; boot test: data bus, address bus and 2 MB patterns at both ends                                                                                                     |
| 16 Mbit SRAM             | FMC bank 3 (NE3), 16-bit, 0x68000000                                 | `hal::SramStm`                                                                             | boot test: data bus, address bus and the full 2 MB                                                                                                                                                    |
| 128 Mbit NOR flash       | FMC bank 1 (NE1), 16-bit, 0x60000000                                 | `hal::NorFlashStm`                                                                         | boot check: identification and the first 4 KB read twice (nothing is written)                                                                                                                         |
| Twin quad-SPI flash      | QUADSPI bank 1: CLK PB2, NCS PG6, IO0 PF8, IO1 PF9, IO2 PF7, IO3 PF6 | `hal::QuadSpiStm`, EMIL `services::FlashGeometryQuadSfdp`, `services::FlashQuadSpiGeneric` | boot check: JEDEC id, SFDP geometry, dummy cycles, first and last 4 KB on one line and on four; `qspitest` saves the last sector, erases it, programs it on four lines, verifies it, then restores it |
| Potentiometer            | PA0_C, ADC1 channel 0                                                | `hal::AdcStm`, `hal::AnalogToDigitalChannelStm`                                            | sampled every 50 ms; shown as a bar and drives the codec volume                                                                                                                                       |
| DAC                      | DAC1 channel 2 on PA5 (CN6 pin 20)                                   | `hal::DacStm`, `hal::DigitalToAnalogPinImplStm`                                            | a 1.3 s triangle wave; shown as a bar                                                                                                                                                                 |
| WM8994 codec             | I2C1 address 0x1A, SAI1 block A: MCLK PE2, SCK PE5, FS PE4, SD PE6   | `hal::SaiOutputStm` (DMA1 stream 0), EMIL `drivers::Wm8994`                                | a 440 Hz tone on the headphone jack (CN17), 48 kHz stereo                                                                                                                                             |
| LEDs                     | PK3 green, PK4 orange (CM4), PK5 red, PK6 blue, active low           | `hal::GpioPinStm`, `services::DebugLed`                                                    | green heartbeat; red for a second after a display or audio underrun; blue while a button, the joystick or the screen is pressed                                                                       |

PLL1 runs the cores at 400 MHz, PLL3 the pixel clock and PLL2 49.152 MHz for SAI1 and the ADC kernel clock.

## Terminal

`help` lists the commands; each one has a short alias.

| Command                      | Alias  | Description                                                                                                 |
|------------------------------|--------|-------------------------------------------------------------------------------------------------------------|
| `sdram`                      | `sd`   | Repeat the SDRAM test on its last 4 MB                                                                      |
| `sram`                       | `sr`   | Repeat the SRAM test                                                                                        |
| `qspi`                       | `q`    | Read the QSPI flash identification, then the first and the last block of the array on one and on four lines |
| `qspitest`                   | `qt`   | Save, erase, program on four lines, verify and restore the last sector of the QSPI flash                    |
| `sdcard`                     | `sc`   | Read, erase, write, verify and restore the last two blocks of the microSD card                              |
| `display`                    | `disp` | Print the DSI, LTDC, clock and frame buffer registers                                                       |
| `panel`                      | `p`    | Reset the panel and initialize it again                                                                     |
| `pattern <0-3>`              | `pt`   | Show a DSI host test pattern: 0 off, 1 and 2 colour bars, 3 BER pattern                                     |
| `dcs <command> [parameters]` | `dc`   | Send a DCS command to the panel, all values in hex                                                          |
| `i2cscan`                    | `i2c`  | List the addresses on I2C1 that acknowledge                                                                 |
| `adc`                        | `a`    | Measure PA1_C                                                                                               |
| `dac <0-4095>`               | `d`    | Hold the DAC output on PA5                                                                                  |
| `wave`                       | `w`    | Run the triangle wave again                                                                                 |
| `loopback`                   | `loop` | Sweep the DAC over five levels and read each back on PA1_C                                                  |
| `volume <0-100>`             | `v`    | Set the codec volume until the potentiometer moves again                                                    |
| `mute`                       | `m`    | Toggle the audio mute                                                                                       |

Touch and joystick events and the boot report are printed as they happen. The boot report is written before the event dispatcher starts, and the 4 KB transmit buffer holds all of it.

## Pins shared between audio and the NOR flash

On the MB1246 the SAI1 pins are also FMC address lines of the NOR flash: PE2 is A23 and PE4 to PE6 are A20 to A22 (solder bridges, closed by default).
hal-st claims a pin once, so the FMC does not own them; the NOR flash is checked at boot with A20 to A22 driven low, which makes its first 2 MB readable, and the SAI takes the pins afterwards. The NOR array is therefore not reachable while the audio runs.
PE3 (A19, also SAI1 SD_B) belongs to the FMC because the SRAM needs it; the demo does not record.

## DAC to ADC loopback

The DAC output is PA5 and the potentiometer sits on PA0_C. Wire PA5 (CN6 pin 20) to PA1_C (CN6 pin 18) and run `loopback`: it prints one line per level, the ADC reading minus the DAC code. Without the wire PA1_C floats and the readings are noise.
The DAC output buffer cannot reach the rails, so the sweep runs from 512 to 3584.

## Not included

- Ethernet, USB OTG, FDCAN, DFSDM microphones, RS-232 as a second port, camera: out of scope for this demo.
- NOR flash writes and the second twin-flash die (QUADSPI bank 2).

## Checked on hardware

Run on an STM32H757I-EVAL (DEV_ID 0x450, revision 0x2003) with an MB1166-A03 display module, through an ST-LINK GDB server and USART1:

- Boot report: SDRAM 32 MB and SRAM 2 MB with 0 errors, NOR manufacturer 0x89 and device 0x227e, QSPI 64 MB with JEDEC id 0x20ba20 (made with a flash class that sent every command on four lines, which a flash in its power-up mode does not understand, so only the identification counts: the access through extended SPI has not been run on this board), MFX id 0x7b with the joystick pins released.
- The display shows the dashboard at about 60 frames per second without underruns while the SDRAM serves the frame buffer. ST's own `LCD_DSI_VideoMode_SingleBuffer` example drives the same module on that board; the demo only matched it once the panel was brought up in ST's order, see `AGENTS.md`.
- The FT6x06 answers at 0x38 with vendor id 0x11 and chip id 0x64 and reports presses, moves and releases.
- `sdram`, `sram`, `qspi`, `adc`, `dac`, `wave`, `volume` and `mute` reply.
- The microSD (`hal::SdCardStm`, SDMMC1, 4-bit behind the level shifter on PC6, PC7 and PB9 with `USE_SD_DIRPOL`) identified a 15193 MiB card (31116288 blocks) and passed the 28 checks of `sdcard` twice, about 137 ms per run, restoring the saved blocks each time.
- The option bytes of that board had BCM4 cleared, so the Cortex-M4 never started and the demo reported it; the fix is in `hal::WaitForCortexM4Stop()` and is not yet re-checked.

Not yet verified: the SDCARD dashboard row on the panel, the no-card path, the microSD pins against the MB1246 schematic (PC6, PC7 and PB9 were taken from the STM32H743I-EVAL and work on this board) and any card power control behind the MFX,
that the touch cursor sits under the finger after the axis orientation was set in the driver, audio, buttons, joystick tiles, LEDs, the DAC output and the PA5 to PA1_C loopback.
