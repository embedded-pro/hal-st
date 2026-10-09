# SD card demo

`examples::SdCardDemo` is the validation test of `hal::SdCardStm`, and it works on any `hal::BlockDevice`. It runs on the STM32F746G-DISCO (`demo/stm32f746g_disco`, at boot) and on the STM32H757I-EVAL Cortex-M7 image (`demo/stm32h757i_eval`, with the `sdcard` terminal command; at boot it only reports the capacity), both with `hal::SdCardStm` on SDMMC1 in 4-bit mode. Both demos set `Config::maxBlocksPerTransfer` to 4, so that the test also exercises the splitting of a request into several transfers.

## What it checks

Everything happens on the last 16 blocks of the card (or from `Config::firstScratchBlock`). They are saved first and written back at the end. Each line is `SD card: PASS <check>` or `SD card: FAIL <check>`:

| Check                                                                                                                   | What it proves                                                                                       |
|-------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------|
| `write N blocks`, `read back N blocks` for N = 1, 2, 5 and 16                                                           | An async write and read of that size completes, and the data read is the data written                |
| `erase 2 blocks`, `erase changed the data`                                                                              | An erase completes and the blocks no longer hold the pattern (the log also shows what they read as)  |
| `erase kept the block before / after its range`, `erase cleared the block in its range`                                 | The erase range is exactly `[begin, end)`: neither neighbour is touched, the block inside is cleared |
| `empty read`, `empty erase`                                                                                             | An empty request completes with `success`                                                            |
| `read last block`                                                                                                       | The last block of the card can be read                                                               |
| `read past the end`, `read across the end`, `write past the end`, `erase across the end`, `erase with end before begin` | A request outside the card completes with `outOfRange` and writes nothing                            |
| `restore verified`                                                                                                      | The saved blocks were written back and read back identical                                           |
| `no completion ran inside its call`                                                                                     | No `onDone` was called before the call that started it returned, for every operation above           |

The last lines are `SD card validation: N/N checks passed` and `SD card demo: PASSED` or `FAILED`. Without a card the demo logs `SD card: not present`. A failed save of the original blocks stops the test before anything is written.

Some cards erase in units larger than a block. If `erase kept the block before / after its range` fails on such a card, the card erased more than it was asked to, not the driver. Say so in the test results rather than ignoring the check.

## Use a scratch card

The test touches only the last 16 blocks (8 KiB) and restores them, but a reset or power loss in the middle of the sequence loses their content. That can include the backup GPT of a GPT-partitioned card. Do not run it on a card you care about.

## Boards

- **STM32F746G-DISCO**: the microSD socket is SDMMC1 on PC8-PC11 (D0-D3), PC12 (CK) and PD2 (CMD); PC13 is the card detect, which the driver does not use. The log is on USART1 (PA9/PB7, the ST-LINK virtual COM port) at the default tracer speed. The SDMMC kernel clock is the 48 MHz PLLQ output of `ConfigureDefaultClockDiscoveryF746G`.
- **STM32H757I-EVAL**: SDMMC1 on the same pins, behind a level shifter whose direction signals are PC6 (`SDMMC1_D0DIR`), PC7 (`SDMMC1_D123DIR`) and PB9 (`SDMMC1_CDIR`). The log is on the existing USART1 tracer, together with the terminal; the MFX card-detect level is traced as `microSD detect` (low means a card is present). The SDMMC kernel clock is the 200 MHz PLL1Q output of `ConfigureDefaultClockEvalH757I`.

The H757I-EVAL has run the previous, shorter version of this test on a 15193 MiB card (31116288 blocks): every step passed. The version above has not run on hardware yet, and the F746G-DISCO has not run either.
