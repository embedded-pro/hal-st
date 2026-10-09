# SD card demo

`examples::SdCardDemo` exercises any `hal::BlockDevice` and logs the result to the global tracer. It runs on the STM32F746G-DISCO (`demo/stm32f746g_disco`, at boot) and on the STM32H757I-EVAL Cortex-M7 image (`demo/stm32h757i_eval`, with the `sdcard` terminal command; at boot it only reports the capacity), both with `hal::SdCardStm` on SDMMC1 in 4-bit mode.

## What it does

On the last two blocks of the card (or `Config::firstScratchBlock`) it:

1. reads the original content,
2. erases the blocks and reads them back (the erased value is card dependent),
3. writes a pattern, reads it back and compares,
4. writes the original content back, reads it back and compares.

Each step logs `PASS` or `FAIL`; the last line is `SD card demo: PASSED` or `FAILED`. Without a card the demo logs `SD card: not present`.

## Use a blank card

The scratch blocks are restored at the end, but a power loss or reset in the middle of the sequence loses their content. Do not run it on a card whose last blocks matter.

## Boards

- **STM32F746G-DISCO**: the microSD socket is SDMMC1 on PC8-PC11 (D0-D3), PC12 (CK) and PD2 (CMD); PC13 is the card detect, which the driver does not use. The log is on USART1 (PA9/PB7, the ST-LINK virtual COM port) at the default tracer speed. The SDMMC kernel clock is the 48 MHz PLLQ output of `ConfigureDefaultClockDiscoveryF746G`.
- **STM32H757I-EVAL**: SDMMC1 on the same pins, behind a level shifter whose direction signals are PC6 (`SDMMC1_D0DIR`), PC7 (`SDMMC1_D123DIR`) and PB9 (`SDMMC1_CDIR`). Those three pins were taken from the STM32H743I-EVAL and still have to be checked against the MB1246 schematic, as does whether the socket's power or detect lines go through the board's I/O expander, which hal-st does not drive. The log is on the existing USART1 tracer, together with the terminal; the MFX card-detect level is traced as `microSD detect`. The SDMMC kernel clock is the 200 MHz PLL1Q output of `ConfigureDefaultClockEvalH757I`.

Neither demo has been run on hardware yet.
