# ST reference image for the STM32H757I-EVAL

`build.sh` builds ST's own `LCD_DSI_VideoMode_SingleBuffer` example for the STM32H747I-EVAL without changing it, against the HAL and CMSIS of this repository. It checks whether a board and its LCD work with ST's software; none of ST's code is stored here, the script clones it from GitHub.

```bash
demo/stm32h757i_eval/st_reference/build.sh
```

The image is `build/st_reference/out/st_reference.elf`, linked for the Cortex-M7 at `0x08000000`. Flash it instead of the demo's Cortex-M7 image; the Cortex-M4 image is not needed.

When the LCD works, the screen turns white with a blue banner that reads `LCD_DSI_VideoMode_SingleBuffer` and two pictures alternate every two seconds. The red LED turns on when the LCD initialization fails.
