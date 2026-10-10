# STM32H745I-DISCO demo

Exercises the on-board peripherals of the STM32H745I-DISCO (MB1381, STM32H745XIH6) through hal-st and EMIL drivers. Trace output and a command terminal share USART3 (PB10 TX, PB11 RX, 115200 8N1) on the ST-LINK virtual COM port.

The Cortex-M7 image runs the demo; the Cortex-M4 image only blinks LD8. Flash both. When the option bytes leave the Cortex-M4 boot disabled (BCM4 cleared), `hal::WaitForCortexM4Stop()` sets `RCC_GCR.BOOT_C2` so the Cortex-M4 still starts; with BCM4 set, the Cortex-M4 boots by itself, stops in `hal::WaitForCortexM7()` and is released by the Cortex-M7 once the trace is up.

```bash
cmake --preset stm32h745-cm7
cmake --build --preset stm32h745-cm7-RelWithDebInfo --target demo_st.stm32h745i_disco_cm7
cmake --preset stm32h745-cm4
cmake --build --preset stm32h745-cm4-RelWithDebInfo --target demo_st.stm32h745i_disco_cm4
```

## What it does

| Part                      | Pins                                                                                                                                                  | Driver                                                                                     | Behaviour                                                                                                                                          |
|---------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------|
| 4.3" 480 x 272 RGB panel  | LTDC RGB888: CLK PI14, HSYNC PI12, VSYNC PI9, DE PK7, data on PH9, PI0, PI1, PI15, PJ0 to PJ15 (without PJ2), PK2 to PK6; DISP PD7, backlight PK0     | `hal::LtdcStm`, `hal::Dma2dStm`                                                            | RGB565 dashboard in SDRAM; DMA2D clears it, the CPU draws the text, the button and the pad; the backlight comes on after the first frame           |
| FT5336 touch controller   | I2C4 PD12/PD13, address 0x38; reset shared with the display connector (LCD_RST, PB12)                                                                 | `hal::I2cStm`, EMIL `drivers::Ft6x06` (`hal::TouchScreen`)                                 | probed 0.6 s after boot, then polled at 50 Hz; a cursor on the second LTDC layer follows the finger and the pad is drawn on                        |
| User button B1            | PC13, active high                                                                                                                                     | `services::DebouncedButton`                                                                | toggles the audio mute                                                                                                                             |
| 128 Mbit SDRAM            | FMC bank 2 (SDNE1, SDCKE1), 16-bit, 8 MB of the 16 MB at 0xD0000000                                                                                   | `hal::SdRamStm`                                                                            | frame buffer in the first 255 KB; boot test: data bus, address bus and 2 MB patterns at both ends                                                  |
| Twin quad-SPI flash       | QUADSPI bank 1: CLK PF10, NCS PG6, IO0 PD11, IO1 PF9, IO2 PF7, IO3 PF6                                                                               | `hal::QuadSpiStm`, EMIL `services::FlashGeometryQuadSfdp`, `services::FlashQuadSpiSingleSpeed` | boot check: JEDEC id, SFDP geometry and the first 4 KB read twice; `qspitest` saves the sector below 16 MB, erases, programs and verifies it, then restores it |
| WM8994 codec              | I2C4 address 0x1A, SAI2 block A: MCLK PI4, SCK PI5, SD PI6, FS PI7                                                                                    | `hal::SaiOutputStm` (DMA2 stream 1), EMIL `drivers::Wm8994`                                | a 440 Hz tone on the headphone jack (CN9), 48 kHz stereo, started with the first frame                                                             |
| I2C4 bus                  | PD12 SCL, PD13 SDA                                                                                                                                    | `hal::I2cStm`, `services::I2cMultipleAccess`                                               | shared by the codec, the touch controller and the scan; 0.5 s after boot the addresses that acknowledge are traced                                  |
| LEDs                      | LD6 red PI13 and LD7 green PJ2 (active low), LD8 PD3 (active high)                                                                                    | `hal::GpioPinStm`, `services::DebugLed`                                                    | LD7 heartbeat of the Cortex-M7, LD8 heartbeat of the Cortex-M4, LD6 while B1 or the screen is pressed                                              |

PLL1 runs the Cortex-M7 at 400 MHz (HCLK 200 MHz), PLL3 the 9.64 MHz pixel clock and PLL2 49.152 MHz for SAI2.

## Power supply

The board feeds the core from the internal SMPS (direct SMPS). `st/CMakeLists.txt` defines `USE_PWR_DIRECT_SMPS_SUPPLY` for this target, so the system file selects it before `main`, and `ConfigureDefaultClockDiscoveryH745I()` asserts it. The setting is write-once until the next power-on reset, and a supply different from the board's makes the ST-LINK lose the target. If that ever happens, UM2488 describes the recovery: move the 10 kOhm resistor from R143 to R144, erase the flash, then move it back. The `info` command and the boot report print `supply: direct SMPS` together with `PWR_CR3`.

## Terminal

`help` lists the commands; each one has a short alias.

| Command    | Alias  | Description                                                                        |
|------------|--------|------------------------------------------------------------------------------------|
| `info`     | `i`    | Print the device, clock and power supply state                                     |
| `sdram`    | `sd`   | Repeat the SDRAM test on its last 4 MB; the first half holds the frame buffer; blocks for about a second |
| `qspi`     | `q`    | Read the QSPI flash identification and the start of the array twice                |
| `qspitest` | `qt`   | Save, erase, program, verify and restore the sector below 16 MB of the first QSPI flash; blocks while the flash is busy |
| `i2cscan`  | `i2c`  | List the addresses on I2C4 that acknowledge, expect 0x1A and 0x38                  |
| `display`  | `d`    | Print the LTDC registers, the PLL3 settings and the frame and underrun counters    |
| `mute`     | `m`    | Toggle the audio mute and trace the new state                                      |
| `clear`    | `c`    | Clear the touch pad                                                                |

Touch events and the boot report are printed as they happen. The boot report is written before the event dispatcher starts, and the 4 KB transmit buffer holds all of it.

## Touch axes

The FT5336 reports portrait coordinates, x along the short side of the panel. `TouchConfig()` gives the driver the controller's own ranges (272 x 480) and swaps the axes, as ST's board support package does for this panel. The touch coordinates are traced, so the orientation can be checked by pressing the corners: the top left should read about 0,0 and the bottom right about 479,271.

## Not included

- Ethernet (LAN8740A on MII), FDCAN1 and FDCAN2 (MCP2562FD), the eMMC (SDMMC1, 8-bit), USB OTG, the PDM microphone (SAI4), the Arduino connectors and the audio input path of the codec: out of scope for this demo.
- The second flash of the twin QSPI memory. The two chips share the clock and BK1_NCS, `hal::QuadSpiStm` drives bank 1 only, so the chip on the bank 2 data lines sees the clock and the chip select without data and is never addressed. Reaching the 128 MB array needs the dual-flash mode in `hal::QuadSpiStm` and a twin-chip geometry in EMIL.
- Quad lines and the rest of the first chip. The flash is in its power-up mode, where a command goes out on one line. EMIL's `FlashQuadSpiGeneric` sends every command on four lines, which only works once the flash is in QPI mode, and `FlashQuadSpi` builds 3-byte addresses, so nothing above 16 MB is reachable. The demo therefore uses `FlashQuadSpiSingleSpeed` and the first 16 MB; the quad data lines IO2 and IO3 are not exercised. `hal::QuadSpiStm::PollStatus` waits without a time limit and blocks the event loop while it does, so a flash that does not answer the status read hangs the core.
- Later revisions of the board (B03 and newer) carry a GT911 touch controller instead of the FT5336; EMIL has no driver for it.

## Checked on hardware

Run on an STM32H745I-DISCO (DEV_ID 0x450, revision 0x2003), both images flashed in one ST-LINK GDB server session, trace and terminal on USART3:

- Boot report: `supply: direct SMPS`, 400 MHz core and 200 MHz HCLK, SDRAM 8192 KB with 0 errors, QSPI JEDEC id 0x20ba20, read on one line. The first `qspitest` on this board hung the Cortex-M7: the old code sent the status read on four lines and with a truncated address, see above; it has been replaced and is not yet run again. The Cortex-M4 stopped and was released (no `did not enter stop mode` line).
- I2C4: the scan finds 0x1A (WM8994) and 0x38 (FT5336) and nothing else; the touch controller answers with vendor id 0x51 and chip id 0x14. The NACK counter grows by 110 per scan and the error counter stays 0.
- The LTDC runs at 60.7 frames per second (607 frames in 10 s) with no underrun, and its timing registers read back as programmed (SSCR 0x00280009, BPCR 0x002a000b, AWCR 0x020a011b, TWCR 0x022a011d, layer window 43 to 522 by 12 to 283, RGB565 at 0xD0000000 with a 960 byte pitch); PLL3 runs at 800 MHz / 83.
- The audio DMA runs without underruns; the tone is started with the first frame.
- `help`, `info`, `i2cscan`, `sdram` (4096 KB, 0 errors), `qspi`, `display` and `mute` reply. The `sdram` retest blocks the event loop for about a second, which costs the audio one underrun.

Not yet verified: the picture on the panel and the backlight, the 440 Hz tone by ear, the touch coordinates and orientation, B1, LD6 to LD8, `qspitest`, and the dashboard rows turning from yellow to green. The panel timing is ST's RK043FN48H timing (HSYNC 41, HFP 32, VSYNC 10, VBP 2, VFP 2) with the back porch cut from 13 to 2 clocks, so that the data starts 43 clocks after the start of HSYNC as it does in ST's board support package for this board; the board support package also keeps the data enable high for 11 more clocks after the last pixel, which the demo does not.
