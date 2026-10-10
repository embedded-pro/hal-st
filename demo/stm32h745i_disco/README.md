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

| Part                     | Pins                                                                                                 | Driver                                                                                     | Behaviour                                                                                                                                                              |
|--------------------------|------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 4.3" 480 x 272 RGB panel | LTDC RGB888 on ports H, I, J and K (28 signals, see `DiscoveryLcdPins.hpp`); DISP PD7, backlight PK0 | `hal::LtdcStm`, `hal::Dma2dStm`                                                            | RGB565 dashboard in SDRAM; DMA2D clears it and the CPU draws; the backlight comes on after the first frame                                                             |
| FT5336 touch controller  | I2C4 PD12/PD13, address 0x38; reset shared with the display connector (LCD_RST, PB12)                | `hal::I2cStm`, EMIL `drivers::Ft6x06` (`hal::TouchScreen`)                                 | probed 0.6 s after boot, then polled at 50 Hz; a cursor on the second LTDC layer follows the finger and the pad is drawn on                                            |
| User button B1           | PC13, active high                                                                                    | `services::DebouncedButton`                                                                | toggles the audio mute                                                                                                                                                 |
| 128 Mbit SDRAM           | FMC bank 2 (SDNE1, SDCKE1), 16-bit, 8 MB of the 16 MB at 0xD0000000                                  | `hal::SdRamStm`                                                                            | frame buffer in the first 255 KB; boot test: data bus, address bus and 2 MB patterns at both ends                                                                      |
| Twin quad-SPI flash      | QUADSPI bank 1: CLK PF10, NCS PG6, IO0 PD11, IO1 PF9, IO2 PF7, IO3 PF6                               | `hal::QuadSpiStm`, EMIL `services::FlashGeometryQuadSfdp`, `services::FlashQuadSpiGeneric` | boot check: JEDEC id, SFDP geometry, dummy cycles, first and last 4 KB on one line and on four; `qspitest` saves, erases, programs, verifies, restores the last sector |
| WM8994 codec             | I2C4 address 0x1A, SAI2 block A: MCLK PI4, SCK PI5, SD PI6, FS PI7                                   | `hal::SaiOutputStm` (DMA2 stream 1), EMIL `drivers::Wm8994`                                | a 440 Hz tone on the headphone jack (CN9), 48 kHz stereo, started with the first frame                                                                                 |
| I2C4 bus                 | PD12 SCL, PD13 SDA                                                                                   | `hal::I2cStm`, `services::I2cMultipleAccess`                                               | shared by the codec, the touch controller and the scan; 0.5 s after boot the addresses that acknowledge are traced                                                     |
| LEDs                     | LD6 red PI13 and LD7 green PJ2 (active low), LD8 PD3 (active high)                                   | `hal::GpioPinStm`, `services::DebugLed`                                                    | LD7 heartbeat of the Cortex-M7, LD8 heartbeat of the Cortex-M4, LD6 while B1 or the screen is pressed                                                                  |

PLL1 runs the Cortex-M7 at 400 MHz (HCLK 200 MHz), PLL3 the 9.64 MHz pixel clock and PLL2 49.152 MHz for SAI2.

## Power supply

The board feeds the core from the internal SMPS (direct SMPS). `st/CMakeLists.txt` defines `USE_PWR_DIRECT_SMPS_SUPPLY` for this target, so the system file selects it before `main`, and `ConfigureDefaultClockDiscoveryH745I()` asserts it.
The setting is write-once until the next power-on reset, and a supply different from the board's makes the ST-LINK lose the target. If that ever happens, UM2488 describes the recovery: move the 10 kOhm resistor from R143 to R144, erase the flash, then move it back.
The `info` command and the boot report print `supply: direct SMPS` together with `PWR_CR3`.

## Terminal

`help` lists the commands; each one has a short alias.

| Command    | Alias | Description                                                                                                                    |
|------------|-------|--------------------------------------------------------------------------------------------------------------------------------|
| `info`     | `i`   | Print the device, clock and power supply state                                                                                 |
| `sdram`    | `sd`  | Repeat the SDRAM test on its last 4 MB (the first half holds the frame buffer); blocks for about a second                      |
| `qspi`     | `q`   | Read the QSPI flash identification, then the first and the last block of the array on one and on four lines                    |
| `qspitest` | `qt`  | Save, erase, program on four lines, verify and restore the last sector of the first QSPI flash; blocks while the flash is busy |
| `i2cscan`  | `i2c` | List the addresses on I2C4 that acknowledge, expect 0x1A and 0x38                                                              |
| `display`  | `d`   | Print the LTDC registers, the PLL3 settings and the frame and underrun counters                                                |
| `mute`     | `m`   | Toggle the audio mute and trace the new state                                                                                  |
| `clear`    | `c`   | Clear the touch pad                                                                                                            |

Touch events and the boot report are printed as they happen. The boot report is written before the event dispatcher starts, and the 4 KB transmit buffer holds all of it.

## Touch axes

The FT5336 reports portrait coordinates, x along the short side of the panel. `TouchConfig()` gives the driver the controller's own ranges (272 x 480) and swaps the axes, as ST's board support package does for this panel.
The touch coordinates are traced, so the orientation can be checked by pressing the corners: the top left should read about 0,0 and the bottom right about 479,271.

## QSPI flash

The flash comes up in its power-up mode (extended SPI), where the command always goes out on one line. `services::FlashGeometryQuadSfdp` reads the geometry, the read opcode and its dummy cycles from the SFDP tables, and `services::FlashQuadSpiGeneric` runs with `Protocol::extendedSpi`:
a read is 1-4-4 (`0xEB`), a program is 1-1-4 (`0x32`), and the write enable, the erase and the status polling stay on one line.
The array is 64 MB, more than a 3-byte address reaches. The SFDP table asks for 4-byte addressing above 16 MB, and the flash class then sends the 4-byte opcodes (`0xEC`, `0x34`, `0x21`) with 4-byte addresses.
`QuadSpiMemory` compares every quad read with a read of the same block on one line (`FlashQuadSpiSingleSpeed`: the plain read, `0x13` with a 4-byte address). The boot check reads the JEDEC id, aligns the dummy cycles (below), then reads the first and the last 4 KB.
A Micron flash keeps the number of dummy cycles of its fast reads in bits 7:4 of its volatile configuration register. The register survives a reset of the processor and is only restored when the flash loses power.
ST's board support package writes 8 into it while the SFDP table gives 10 for the 1-4-4 read, so a board that ran ST's firmware since its last power-up returns the four-line data two clocks late, one byte ahead.
The check reads the register and, when its field holds an explicit number other than the geometry's, writes it back with the geometry's number (write enable, `0x81`) and reads it again; the boot trace says which of the two happened (`QSPI flash dummy cycle field ...`).
`qspitest` works on the last 4 KB sector, whose address is 0x3FFF000. It saves the sector and the one that a truncated address would reach (0xFFF000), erases the sector, checks that it is blank, programs 256 bytes on four lines, verifies them on four and on one line, checks that the sector at 0xFFF000 is unchanged, then erases again, writes the saved data back and verifies it.
If the power fails in the middle, that one sector can stay erased.
`hal::QuadSpiStm::PollStatus` waits without a time limit and blocks the event loop while it does, so a flash that does not answer the status read hangs the core.
The two chips share the clock and BK1_NCS and `hal::QuadSpiStm` drives bank 1 only, so the chip on the bank 2 data lines sees the clock and the chip select without data and is never addressed. Reaching the 128 MB array needs the dual-flash mode in `hal::QuadSpiStm` and a twin-chip geometry in EMIL.

## Not included

- Ethernet (LAN8740A on MII), FDCAN1 and FDCAN2 (MCP2562FD), the eMMC (SDMMC1, 8-bit), USB OTG, the PDM microphone (SAI4), the Arduino connectors and the audio input path of the codec: out of scope for this demo.
- Later revisions of the board (B03 and newer) carry a GT911 touch controller instead of the FT5336; EMIL has no driver for it.

## Checked on hardware

Run on an STM32H745I-DISCO (DEV_ID 0x450, revision 0x2003), both images flashed in one ST-LINK GDB server session, trace and terminal on USART3:

- Boot report: `supply: direct SMPS`, 400 MHz core and 200 MHz HCLK, SDRAM 8192 KB with 0 errors, QSPI JEDEC id 0x20ba20 read on one line. The Cortex-M4 stopped and was released (no `did not enter stop mode` line).
- I2C4: the scan finds 0x1A (WM8994) and 0x38 (FT5336) and nothing else; the touch controller answers with vendor id 0x51 and chip id 0x14. The NACK counter grows by 110 per scan and the error counter stays 0.
- The LTDC runs at 60.7 frames per second (607 frames in 10 s) with no underrun, and its timing registers read back as programmed (SSCR 0x00280009, BPCR 0x002a000b, AWCR 0x020a011b, TWCR 0x022a011d, layer window 43 to 522 by 12 to 283, RGB565 at 0xD0000000 with a 960 byte pitch); PLL3 runs at 800 MHz / 83.
- The audio DMA runs without underruns; the tone is started with the first frame.
- `help`, `info`, `i2cscan`, `sdram` (4096 KB, 0 errors), `qspi`, `display` and `mute` reply, and `mute` traces `audio muted` and `audio unmuted`. The `sdram` retest blocks the event loop for about a second, which costs the audio one underrun.
- By eye and ear: LD7 and LD8 blink, the dashboard is sharp and correctly placed, the 440 Hz tone is audible, and the touch coordinates and the cursor are correct at the corners and the centre.
- The first `qspitest` hung the Cortex-M7: the flash class then in use sent the status read on four lines to a flash in single-line mode, with a truncated address, and the boot-time array read returned a constant 0x88.
  The flash classes in EMIL have been reworked since (see QSPI flash) and the new access has not been run on the board yet; the QSPI dashboard row also stayed yellow until the start-up drawing race was fixed.

Not yet verified: the QSPI check and `qspitest` after the rework, the QSPI row turning green at boot, B1 and LD6, and the BCM4-cleared start path.
The panel timing is `boards::rk043fn48hTiming` from EMIL, ST's RK043FN48H timing (HSYNC 41, HFP 32, VSYNC 10, VBP 2, VFP 2) with the back porch cut from 13 to 2 clocks, so that the data starts 43 clocks after the start of HSYNC as it does in ST's board support package for this board;
the board support package also keeps the data enable high for 11 more clocks after the last pixel, which the demo does not.
