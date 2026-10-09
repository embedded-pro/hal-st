# hal-st — Agent Rules (canonical)

Single source of truth for **Claude, Copilot, and sub-agents**. `CLAUDE.md` points here. Detailed C++ coding rules: `.github/instructions/hal-st-cpp.instructions.md` (binding for all `*.hpp/*.cpp/*.h/*.c` changes). Copilot custom agents: `.github/agents/`. Build presets: `CMakePresets.json`.

hal-st is a Hardware Abstraction Layer for ST ARM Cortex-M microcontrollers (F4, F7, G0, G4, H5, H7, WB, WBA families), implementing [embedded-infra-lib](https://github.com/embedded-pro/embedded-infra-lib) HAL interfaces over the STM32 HAL/LL library. It's a copy of [philips-software/amp-hal-st](https://github.com/philips-software/amp-hal-st).

## Architecture

- `hal_st/stm32fxxx/` — STM32 peripheral drivers (Uart, Can, Spi, Adc, Gpio, Dma, Timer, Flash, Ethernet, USB, parallel memories on FSMC/FMC — `FmcStm` controller with `SramStm`, `NorFlashStm`, `SdRamStm` banks, …), with `ip/` and `mcu/` holding the ST pin-data XML (GPIO alternate functions, per-MCU peripheral lists) the build turns into `PeripheralTable`/`PinoutTableDefault`
- Display: `LtdcStm` (`hal::DisplayController`), `Dma2dStm` (`hal::Blitter`) and `DsiHostStm` (`hal::DsiHost` + `hal::DsiVideoStream`) implement interfaces hosted in embedded-infra-lib (`hal/interfaces`). LTDC and DMA2D exist on F429, F746/F767 and H757, the DSI host on H757 only; the files compile to nothing elsewhere
- Display pins and memory: LTDC pins are one `PinConfigTypeStm::ltdc*` per signal because some pins carry two LTDC signals on different alternate functions. Frame buffers must be reachable by the LTDC and DMA2D (not DTCM/CCM); nothing here does cache maintenance. The LTDC layer registers are shadowed and a read returns the active value: write `LxCR` whole, never read-modify-write it between `ConfigureLayer` and `Commit` (the HAL's CLUT calls did and left every layer disabled)
- Display panels: controller set-up (ILI9341, OTM8009A command tables) lives in EMIL `drivers/display` and `boards` (`Stm32f429iDiscoLcdSetup`, `Mb1166Setup`); the demos only wire it to the drivers
- Audio pins and tables: `PinConfigTypeStm::sai*` (SCK, MCLK, SD, FS, and the PDM CK1/CK2/D1–D3; blocks A and B share one type per signal) and `i2s*` (CK, WS, MCK, SD; the SDI/SDO split of H5/H7 maps to the one `i2sSd`). I2S has no `PeripheralTable` entry because I2Sx is SPIx: it uses the `Spi` arrays (`PeripheralTableH7xx.xml` gained `Spi` and `Sai`, `PeripheralTableH5xx.xml` gained `Sai`). `HAS_PERIPHERAL_SAI` exists on F429, F746/F767, G4, WB55, WBA55/65, H5 and H7; `ResetPeripheralSai` is generated next to `ResetPeripheralSpi`. WBA55 uses its own pin data (`mcu/STM32WBA55CGUx.xml` with `ip/GPIO-STM32WBAx`) because the WBA52 data has no SAI1 block A/B. The pin generator skips F4 `I2Sx_ext_SD` signals
- `hal_st/synchronous_stm32fxxx/` — Blocking driver variants (`SynchronousUart`, `SynchronousSpiMaster`, …)
- `hal_st/instantiations/` — Board event infrastructure (`StmEventInfrastructure`, `NucleoUi`, `DiscoveryUi`)
- `hal_st/bringup/` — Startup glue (`Default_Handler_Forwarded`, HAL tick/assert hooks); the Cortex-M core code (`InterruptCortex`, `SystemTick`, …) comes from EMIL `hal.cortex_m`
- `hal_st/middlewares/` — `STM32_WPAN`, `ble_middleware`
- `hal_st_lwip/` — lwIP network stack instantiations
- `st/` — CMSIS headers, STM32 HAL driver sources (per family), `hal_conf/`, `ldscripts/`
- `services/st_util/` — ST bootloader communicator services
- `integration_test/` — hardware-in-the-loop cucumber test rig (`pcb/`, `flasher/`, `tester/`, `tested/`, `runner/`, `logic/`)
- `validation/` — hardware-in-the-loop validation app (NUCLEO-WB55RG, NUCLEO-WBA55CG): `firmware/` (target `hal_st.validation_firmware`, every driver behind EMIL's `services/hil` terminal), `host/` (Python package `hal_st_validation` + pytest suite driving the firmware and a Digilent Analog Discovery 3); command set in `validation/PROTOCOL.md`
- `examples/` — `blink`, `helloworld`, `sesame`, `freertos` (generic, `examples_st.*` targets, `HALST_BUILD_EXAMPLES`)
- `demo/` — board demos (`demo_st.*` targets, `HALST_BUILD_DEMOS`): `display_demo` (shared, interface-only) and `audio_demo` (shared tone generator) as support libraries, `stm32f746g_disco` (SAI2 + WM8994), `stm32f407g_disco` (LIS3DSH, CS43L22, MP45DT02, RTT), `stm32f429i_disco` (LTDC + DMA2D + SDRAM, ILI9341 set up over SPI5 by EMIL's `boards.stm32f429i_disco_lcd`, gyroscope sharing SPI5, STMPE811 touch, DAC-to-ADC loopback, USART1 terminal), `stm32h757i_eval` (CM7: DSI panel dashboard in SDRAM, FT6x06 touch, MFX joystick, FMC SDRAM/SRAM/NOR, QSPI flash, ADC potentiometer, DAC, SAI1 + WM8994, USART1 terminal), and `stm32wb_stm32wba_nucleo/` (`ble_peripheral`, `ble_central` for NUCLEO-WB55RG and NUCLEO-WBA55CG)

## Memory — no heap

This is a driver library that always ends up running on constrained MCUs. Forbidden everywhere: `new`/`delete`/`malloc`/`free`, `make_unique`/`make_shared`, `std::vector`/`string`/`deque`/`list`/`map`/`set`. No recursion in driver code — stack depth must be statically bounded.

Use: `infra::BoundedVector<T>`, `infra::BoundedString`, `infra::BoundedDeque<T>`, `infra::MemoryRange<T>` (buffer params, not raw pointer+size), `std::array<T,N>`, `std::optional<T>`.

## STM32 HAL/LL & driver conventions

Full detail lives in `.github/instructions/hal-st-cpp.instructions.md` — read it before touching driver code. Key points:

- `HAL_*`/`LL_*` only; never write to registers via magic offsets
- `HAL_FOO_Init` in constructor, `HAL_FOO_DeInit` + clock disable in destructor (RAII)
- Interrupt handlers: `private InterruptHandler` (single-vector) or `DispatchedInterruptHandler` (multi-vector, one member per vector); never call `NVIC_EnableIRQ` directly
- Every alternate-function pin: a `PeripheralPinStm` member, declared in constructor-init order
- Every driver: inner `Config` struct with mandatory `constexpr Config() {}` and sensible field defaults
- `oneBasedIndex` convention for peripheral indices; `really_assert` bounds; table access as `table[oneBasedIndex - 1]`
- `HAS_PERIPHERAL_xxx` guards come from generated `PeripheralTable.hpp` — never hand-edit anything under `generated/`
- DMA: `DMA_STREAM_BASED` (F4/F7/H7; on H7 `DmaChannelId::channel` is the DMAMUX1 request, `DMA_REQUEST_*`; DMA1/DMA2 only, BDMA is not supported) vs `DMA_CHANNEL_BASED` (G0/G4/WB/WBA/H5) — use `hal_st` DMA wrappers, not raw HAL DMA handles (the one exception is `DcmiStm`, see "DCMI camera capture")
- Naming: `FooStm` drivers, `SynchronousFooStm` blocking variants

## STM32H7 (H757, dual-core)

- One preset builds one core's image: `TARGET_CORTEX` (`m7`|`m4`) defines `CORE_CM7`/`CORE_CM4` and selects `startup_stm32h757xx[_cm4].s` and `st/ldscripts/mem_stm32h757_c{m7,m4}.ld` (CM7 flash `0x08000000`/AXI SRAM, CM4 flash `0x08100000`/SRAM1-3). Flash both images. `hal::WaitForCortexM4Stop()` sets `RCC_GCR.BOOT_C2` when the option bytes have BCM4 cleared (seen on an MB1246), otherwise the CM4 never runs.
- `system_stm32h7xx_dualcore_boot_cm4_cm7.c` is the only system file compiled (`add_hal_driver(… SYSTEM_SOURCE …)`). CM7 first waits for the CM4 domain to stop (`hal::WaitForCortexM4Stop()`, before touching any D2 peripheral), configures the clocks and then calls `hal::ReleaseCortexM4()`; CM4 calls `hal::WaitForCortexM7()` before `HAL_Init()` (`hal_st/stm32fxxx/DualCoreHandshakeStm`).
- Per-core peripheral state (EXTI mask/pending) goes through `EXTI_D1` (CM7) / `EXTI_D2` (CM4); don't share GPIO ports between cores without HSEM arbitration.
- Drivers not yet ported to H7 are excluded in `hal_st/stm32fxxx/CMakeLists.txt` (`HEADER_FILE_ONLY`) and `hal_st/synchronous_stm32fxxx/CMakeLists.txt`; shrink those lists as drivers are ported. `PeripheralTableH7xx.xml` has `I2c`, `Adc`, `Dac` and `QuadSpi` besides the display, FMC, SAI and DCMI entries.
- H7 I2C (`I2cStm`) shares the WB/WBA code paths (own-address 1 disabled, NACK and bus-error recovery) and defaults to 400 kHz for a 100 MHz kernel clock. H7 ADC (`AdcStm`) uses `ADC_CLOCK_ASYNC_DIV4`: the application selects the ADC kernel clock (`RCC_PERIPHCLK_ADC`; after reset it is PLL2 P, which is off) and `AnalogToDigitalChannelStm` measures a channel that has no GPIO, such as the `_C` pads (PA0_C is ADC1/2 channel 0).
- MB1246 (EVAL) board support lives in `demo/stm32h757i_eval/` (active-low LEDs and the tamper button via `InvertedGpioPin`, `DefaultClockEvalH757I`, USART1 tracer and terminal on `hal::UartStm`). `stm32h757-cm7` builds `demo_st.stm32h757i_eval_cm7` (the demo, see its README), `stm32h757-cm4` builds `demo_st.stm32h757i_eval_cm4` (orange LED).
- The CM7 image of `demo/stm32h757i_eval/` draws a dashboard on the 800 x 480 DSI panel (OTM8009A set up by EMIL's `boards.mb1166`, RGB565 frame buffer in the first 768 KB of the SDRAM, cursor layer in AXI SRAM, pixel clock from PLL3 via `ConfigureLtdcClockEvalH757I`)
- MB1246 pin sharing: PE2 and PE4 to PE6 are SAI1 MCLK/FS/SCK/SD and also FMC A23, A20 to A22 of the NOR flash. The demo keeps them out of the FMC pin list, drives A20 to A22 low while it checks the NOR identification at boot and hands the pins to the SAI afterwards, so the NOR array is only reachable before the audio starts. The FMC pin list must be one `FmcStm` because a pin is claimed once
- `DsiHostStm` sends every DCS and generic command in low power (`HAL_DSI_ConfigCommand`; `Config::commandsInLowPower` turns it off) and only programs the D-PHY timers when `Config::phyTimer` is set, so the timers, host timeouts and receive filter keep the reset values that ST's OTM8009A bring-up relies on. `Config::lowPowerReceiveFilter` is optional for the same reason
- Panel bring-up on the MB1246: with EMIL's own order (DSI host running, then the panel reset, 0x05 final writes, PHY timers of 35) the OTM8009A kept showing its frame memory, static coloured pixels, even for the DSI host test pattern. ST's `LCD_DSI_VideoMode_SingleBuffer` drives the same module on that board
- The demo follows that example: it resets the panel and switches the backlight on before `DsiHostStm` exists (EMIL gets a pin that ignores its own reset pulse), starts the stream before the panel init and ends it with a no-operation and a memory write start as one-parameter short writes. The steps were not tried one by one; ST's `LCD_DSI_VideoMode_SingleBuffer` is the register reference
- Local vendor patches to keep on re-import: every `gcc/startup_*.s` (`Default_Handler_Forwarded`) and the `.cpu cortex-m4` copy `startup_stm32h757xx_cm4.s`; `hal_conf/stm32h7xx_hal_conf.h` (`hse_value`, `stm32_assert.h`, USART/COMP aliases); pin-data XML namespace rewritten to `http://mcd.rou.st.com/modules.php?name=mcu`; `GeneratePinoutTableStructure.xsl` skips `*_C` analog pads.
- Local vendor patches to keep on re-import (display): `USE_HAL_LTDC_REGISTER_CALLBACKS` and `USE_HAL_DMA2D_REGISTER_CALLBACKS` set to `1U` in `hal_conf/stm32{f4,f7,h7}xx_hal_conf.h`, because `LtdcStm` and `Dma2dStm` register their callbacks

## DCMI camera capture (`DcmiStm`)

- `DcmiStm` implements EMIL `hal::Camera` on F4, F7, H5 and H7, the families that have a DCMI; it compiles to nothing where `HAS_PERIPHERAL_DCMI` is undefined. On H5 the DCMI shares its interrupt and clock with PSSI. Its pins are one `MultiGpioPinStm` (D0..Dn, HSYNC, VSYNC, PIXCLK, `PinConfigTypeStm::dcmi`); XCLK for the sensor is the application's job (timer or MCO).
- It owns a raw `DMA_HandleTypeDef` and calls `HAL_DCMI_Start_DMA` instead of using `DmaStm`: `DmaStm` takes `uint16_t` byte counts (a QVGA RGB565 frame is 153600 bytes) and has no double-buffer mode, which the ST HAL uses to split frames of more than 65535 words. On H5 it builds the one-node, or circular two-node, GPDMA linked-list queue that `HAL_DCMI_Start_DMA` expects. It enables the DMA clocks and never disables them, and it does not reserve its stream in `DmaStm`; a conflict with another user of the stream shows up as a second registration of the same IRQ.
- Both its vectors (DCMI and DMA stream) use `ImmediateInterruptHandler` at the same priority: the HAL re-programs the memory addresses of a split frame from the DMA ISR, so deferring that work to the event dispatcher would corrupt frames silently. The HAL callbacks only post an event; `onFrame` and `onError` run on the event dispatcher.
- It registers the HAL frame and error callbacks, so `USE_HAL_DCMI_REGISTER_CALLBACKS` is `1U` in the F4, F7, H5 and H7 `hal_conf` headers (keep on re-import).
- Frame buffers are caller-owned: 4-byte aligned, a whole number of words, at most 262140 bytes per DMA pass (65535 on H5) unless the frame divides evenly into a power-of-two number of passes, and not in TCM. A JPEG frame is snapshot-only and must fit in one pass. On F7/H7 with the D-cache enabled they must be 32-byte aligned and sized; the driver cleans and invalidates the cache around a capture.

## Audio (`SaiOutputStm`, `SaiInputStm`, `I2sOutputStm`, `I2sInputStm`)

- They implement EMIL `hal::AudioOutput` / `hal::AudioInput` (EMIL `docs/ExecutionModel.md`, "Streaming audio"), one class per direction. SAI: `SaiStm` owns the instance (clock, reset) and each block (`Config::block`, A or B) is one object; a block that is `synchronousToOtherBlock` must be a slave, is started before the block that provides its clocks and stopped before it (both are asserted). I2S: one object per SPI instance, half duplex. SAI exists on F429, F746/F767, G4, WB55, WBA55/65, H5 and H7 (SAI1–3; SAI4 sits behind BDMA); I2S on F4, F7, G4, H5 and H7. G070 I2S is not supported because `DmaStm` cannot drive its shared DMA vectors
- The format arrives in `Start()`, so `HAL_SAI_InitProtocol` / `HAL_I2S_Init` run there and `HAL_*_DeInit` in `Stop()`; the constructor only claims clock, pins and the DMA stream. The data is 16-bit I2S (Philips), master or slave. Mono: SAI uses its mono mode, I2S duplicates a mono output into both slots and keeps the left slot of a mono input, in place
- `AudioDmaOutputStm` / `AudioDmaInputStm` cycle a caller-owned buffer (`WithBuffer<N>`, 32-byte aligned) with a `DmaStm` circular channel. The buffer is two periods and a period is a half: an even size, whole frames, at most 65535 bytes, not in TCM, and a multiple of 64 bytes where the D-cache is on (they clean and invalidate around each period). The DMA interrupt only records the completed half and schedules one event, so half and full-transfer events never coalesce; the callback runs on the event dispatcher, and a callback that is not done before the DMA wraps into its half, or a dispatcher more than a period late, gets `onUnderrun` / `onOverrun` while the stream goes on. The first two periods of an output are silence. `SetVolume` / `SetMuted` are software (linear gain on the filled period). `Stop(onStopped)`, `SetVolume(percent, onDone)` and `SetMuted(muted, onDone)` take effect at once and schedule their callback on the event dispatcher; a period already scheduled when the stream stops is dropped, so no stream callback runs after `Stop()` returns
- The application owns the kernel clock. The SAI driver picks the master clock divider itself, because `HAL_SAI_Init` asserts (and aborts) on any rate outside its standard list, and range-checks it against the MCKDIV field; `HAL_I2S_Init` derives the divider and the driver reads it back. Either way `ActualSampleRate()` is the rate the divider really gives and the driver asserts when it is more than `maxRateErrorPermille` (default 10) off the request, so G4 and WBA, which have no audio PLL, only reach rates their system PLL divides to. `Config::kernelClock` only applies to F4 SAI and `mclkOutput` only to the SAI of G4, WB, WBA, H5 and H7 (the older SAI always drives MCLK) `AudioClockSwitchStm` applies one of two application-supplied `RCC_PeriphCLKInitTypeDef` (the 48 kHz or the 44.1 kHz family) when a master stream starts: pass it in `Config::audioClock` and do not start a stream of the other family while one runs
- PDM: `Config::mode = pdm` on an input captures one microphone. The format is EMIL's raw one (rate = clock / 16, one channel), a frame of two 16-bit slots is read as two consecutive words, and `pdmSampleEdge` selects the clock edge the receiver samples the data line on (rising by default; match it to how the microphone's select pin is strapped). The SAI PDM interface for several microphones is not used; its pin types (`saiCk1`, `saiCk2`, `saiD1`–`saiD3`) exist
- Demos: `demo/audio_demo` (a tone generator on `hal::AudioOutput`) and `demo/stm32f746g_disco` (SAI2 block A into EMIL `drivers::Wm8994` over I2C3). The board has the TFBGA216 part, whose pin data is `mcu/STM32F746NGHx.xml`, so the example is only built by the `stm32f746g-disco` preset (`TARGET_MCU_VARIANT=stm32f746ng`); its DMA stream and PLLI2S numbers are the ones of ST's board support package. `demo/stm32h757i_eval` plays a tone through the WM8994 on SAI1 block A (DMA1 stream 0, `DMA_REQUEST_SAI1_A`) with SAI1 clocked from PLL2 P (49.152 MHz) by `ConfigureAudioClockEvalH757I`. `demo/stm32f407g_disco` drives EMIL `drivers::Cs43l22` from `I2sOutputStm` on I2S3 and EMIL `drivers::Mp45dt02` from `I2sInputStm` (PDM) on I2S2 with the app-side `PdmDecimator`; both I2S share one fixed PLLI2S, and the example only exists when `EMIL_INCLUDE_SEGGER_RTT` is set (the `stm32f407` preset does)
- Only `demo/stm32f407g_disco` has run on hardware: `I2sOutputStm` playing a tone through the CS43L22 and `I2sInputStm` in PDM mode running without overruns. `validation/` does not cover audio

## Style

- Allman braces, 4-space indent, `.clang-format` authoritative
- PascalCase types/methods, camelCase members/locals; `const`-correct on all observer/query methods
- `#pragma once` for new/modified headers; legacy `#ifndef` guards may stay untouched
- No C-style casts — `static_cast<>`; `reinterpret_cast<>` only where the HAL requires register/void-pointer casts
- **No comments** except non-obvious *why*. No `TODO`/`FIXME`/`HACK`, no commented-out code

## Interfaces & errors

- Interfaces = pure virtual; `virtual ~I() = default` — **never** `= 0` destructors. Interfaces hosted in embedded-infra-lib follow that repo's style instead (protected non-virtual destructor)
- No exceptions. `std::optional<T>` or status enums. `really_assert()` for preconditions
- No global mutable state — all state lives in driver class members

## Testing

No unit tests in this repo. hal-st is validated on real hardware — the `validation/` app (firmware + pytest-bdd/AD3 host suite: Gherkin scenarios in `validation/host/tests/hil/features/`, steps in `validation/host/tests/hil/test_*.py`; see `validation/README.md`), manual testing on Nucleo/Discovery boards,
logic-analyser/scope verification and the `integration_test/` rig — not by GoogleTest suites. A driver change on STM32WB55/WBA55 should keep `validation/PROTOCOL.md`, the firmware factory and the host
tests in step. `validation/host/tests/unit` tests the host harness itself (`pytest validation/host/tests/unit`), not the drivers. Don't add unit tests for new or changed drivers.
(`services/st_util/test/` is a pre-existing exception gated behind `HALST_BUILD_TESTS`; leave it as-is, don't extend the pattern elsewhere.)

## Build

```bash
cmake --preset host && cmake --build --preset host-Debug   # host tooling/build check
cmake --preset stm32f407 && cmake --build --preset stm32f407-RelWithDebInfo   # embedded target
```

Other target presets: `stm32f746g-disco` (STM32F746G-DISCO, TFBGA216 pin data), `stm32wb55`, `stm32g070`, `stm32g431`, `stm32f429`, `stm32f746`, `stm32f767`, `stm32g474`, `stm32wba52`, `stm32wba55`, `stm32wba65`, `stm32h563`, `stm32h573`, `stm32h757-cm7`, `stm32h757-cm4`.

Validation firmware (stm32wb55, stm32wba55): `cmake --build --preset stm32wb55-RelWithDebInfo --target hal_st.validation_firmware`.

## Assistant behavior — be terse

- Minimal prose. No preamble/postamble, no restating the plan, no summaries unless asked
- Report results as file paths + build pass/fail (no test suite to report)
- Don't re-read files already read; batch reads; prefer targeted edits
