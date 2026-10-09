#!/bin/bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
deps=${1:-$repo/build/st_reference/deps}
out=${2:-$repo/build/st_reference/out}

mkdir -p "$deps" "$out"
cd "$deps"

clone()
{
    [ -d "$2" ] || git clone -q --depth 1 "https://github.com/STMicroelectronics/$1.git" "$2"
}

clone stm32h747i-eval-bsp BSP/STM32H747I-EVAL
clone stm32-bsp-common BSP/Components/Common
clone stm32-otm8009a BSP/Components/otm8009a
clone stm32-adv7533 BSP/Components/adv7533
clone stm32-is42s32800j BSP/Components/is42s32800j
clone stm32-mfxstm32l152 BSP/Components/mfxstm32l152

if [ ! -d cube ]; then
    git clone -q --depth 1 --filter=blob:none --sparse https://github.com/STMicroelectronics/STM32CubeH7.git cube
    git -C cube sparse-checkout set Projects/STM32H747I-EVAL/Examples/LCD_DSI/LCD_DSI_VideoMode_SingleBuffer Utilities/lcd Utilities/Fonts Utilities/CPU
fi

example=$deps/cube/Projects/STM32H747I-EVAL/Examples/LCD_DSI/LCD_DSI_VideoMode_SingleBuffer
hal=$repo/st/STM32H7xx_HAL_Driver
cmsis=$repo/st/CMSIS_STM32H7xx
bsp=$deps/BSP

cflags="-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -Os -g -ffunction-sections -fdata-sections -std=gnu11 -w
    -DSTM32H747xx -DUSE_HAL_DRIVER -DUSE_STM32H747I_EVAL -DUSE_LCD_HDMI -DUSE_PWR_DIRECT_SMPS_SUPPLY -DUSE_IOEXPANDER -DCORE_CM7 -DDEBUG
    -I$example/Common/Inc -I$example/CM7/Inc -I$hal/Inc -I$cmsis/Core/Include -I$cmsis/Device/ST/STM32H7xx/Include
    -I$bsp/STM32H747I-EVAL -I$bsp/Components/Common -I$deps/cube/Utilities/lcd -I$deps/cube/Utilities/Fonts -I$deps/cube/Utilities/CPU"

sources=$(ls "$hal"/Src/stm32h7xx_hal*.c "$hal"/Src/stm32h7xx_ll_*.c | grep -v template)
sources="$sources
    $example/CM7/Src/main.c $example/CM7/Src/stm32h7xx_it.c $example/CM7/Src/stm32h7xx_hal_msp.c $example/Common/Src/system_stm32h7xx.c
    $example/STM32CubeIDE/CM7/Example/User/CM7/syscalls.c $example/STM32CubeIDE/CM7/Example/User/CM7/sysmem.c
    $bsp/STM32H747I-EVAL/stm32h747i_eval.c $bsp/STM32H747I-EVAL/stm32h747i_eval_bus.c $bsp/STM32H747I-EVAL/stm32h747i_eval_io.c
    $bsp/STM32H747I-EVAL/stm32h747i_eval_lcd.c $bsp/STM32H747I-EVAL/stm32h747i_eval_sdram.c
    $bsp/Components/otm8009a/otm8009a.c $bsp/Components/otm8009a/otm8009a_reg.c
    $bsp/Components/adv7533/adv7533.c $bsp/Components/adv7533/adv7533_reg.c
    $bsp/Components/is42s32800j/is42s32800j.c
    $bsp/Components/mfxstm32l152/mfxstm32l152.c $bsp/Components/mfxstm32l152/mfxstm32l152_reg.c
    $deps/cube/Utilities/lcd/stm32_lcd.c"

objects=""
for source in $sources; do
    object=$out/$(basename "$source" .c).o
    arm-none-eabi-gcc $cflags -c "$source" -o "$object"
    objects="$objects $object"
done

arm-none-eabi-gcc $cflags -x assembler-with-cpp -c "$example/STM32CubeIDE/CM7/Example/User/Startup/startup_stm32h747xihx.s" -o "$out/startup.o"
arm-none-eabi-gcc -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -specs=nosys.specs \
    -T"$example/STM32CubeIDE/CM7/STM32H747XIHX_FLASH.ld" -Wl,--gc-sections -Wl,-Map="$out/st_reference.map" \
    $objects "$out/startup.o" -lc -lm -o "$out/st_reference.elf"
arm-none-eabi-size "$out/st_reference.elf"
