# hal-st — Agent Rules (canonical)

Single source of truth for **Claude, Copilot, and sub-agents**. `CLAUDE.md` points here. Detailed C++ coding rules: `.github/instructions/hal-st-cpp.instructions.md` (binding for all `*.hpp/*.cpp/*.h/*.c` changes). Copilot custom agents: `.github/agents/`. Build presets: `CMakePresets.json`.

hal-st is a Hardware Abstraction Layer for ST ARM Cortex-M microcontrollers (F4, F7, G0, G4, H5, H7, WB, WBA families), implementing [embedded-infra-lib](https://github.com/embedded-pro/embedded-infra-lib) HAL interfaces over the STM32 HAL/LL library. It's a copy of [philips-software/amp-hal-st](https://github.com/philips-software/amp-hal-st).

## Architecture

- `hal_st/stm32fxxx/` — STM32 peripheral drivers (Uart, Can, Spi, Adc, Gpio, Dma, Timer, Flash, Ethernet, USB, parallel memories on FSMC/FMC — `FmcStm` controller with `SramStm`, `NorFlashStm`, `SdRamStm` banks, …), with `ip/` and `mcu/` holding the ST pin-data XML (GPIO alternate functions, per-MCU peripheral lists) the build turns into `PeripheralTable`/`PinoutTableDefault`
- Display: `LtdcStm` (`hal::DisplayController`), `Dma2dStm` (`hal::Blitter`) and `DsiHostStm` (`hal::DsiHost` + `hal::DsiVideoStream`) implement interfaces hosted in embedded-infra-lib (`hal/interfaces`). LTDC and DMA2D exist on F429, F746/F767 and H757, the DSI host on H757 only; the files compile to nothing elsewhere
- Display pins and memory: LTDC pins are one `PinConfigTypeStm::ltdc*` per signal because some pins carry two LTDC signals on different alternate functions. Frame buffers must be reachable by the LTDC and DMA2D (not DTCM/CCM); nothing here does cache maintenance
- Display panels: controller set-up (ILI9341, OTM8009A command tables) lives in EMIL `drivers/display` and `boards` (`Stm32f429iDiscoLcdSetup`, `Mb1166Setup`); the examples only wire it to the drivers
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
- `examples/` — `blink`, `helloworld`, `sesame`, `freertos`, `ble_peripheral`, `ble_central`, `display_demo` (shared, interface-only), `stm32f429i_disco` (LTDC + DMA2D + SDRAM, ILI9341 set up over SPI5 by EMIL's `boards.stm32f429i_disco_lcd`), `stm32h757i_eval`

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

- One preset builds one core's image: `TARGET_CORTEX` (`m7`|`m4`) defines `CORE_CM7`/`CORE_CM4` and selects `startup_stm32h757xx[_cm4].s` and `st/ldscripts/mem_stm32h757_c{m7,m4}.ld` (CM7 flash `0x08000000`/AXI SRAM, CM4 flash `0x08100000`/SRAM1-3). Flash both images; default option bytes boot both cores.
- `system_stm32h7xx_dualcore_boot_cm4_cm7.c` is the only system file compiled (`add_hal_driver(… SYSTEM_SOURCE …)`). CM7 first waits for the CM4 domain to stop (`hal::WaitForCortexM4Stop()`, before touching any D2 peripheral), configures the clocks and then calls `hal::ReleaseCortexM4()`; CM4 calls `hal::WaitForCortexM7()` before `HAL_Init()` (`hal_st/stm32fxxx/DualCoreHandshakeStm`).
- Per-core peripheral state (EXTI mask/pending) goes through `EXTI_D1` (CM7) / `EXTI_D2` (CM4); don't share GPIO ports between cores without HSEM arbitration.
- Drivers not yet ported to H7 are excluded in `hal_st/stm32fxxx/CMakeLists.txt` (`HEADER_FILE_ONLY`) and `hal_st/synchronous_stm32fxxx/CMakeLists.txt`; shrink those lists as drivers are ported.
- MB1246 (EVAL) board support lives in `examples/stm32h757i_eval/` (active-low LEDs via `InvertedGpioPin`, `DefaultClockEvalH757I`, USART1 tracer). `stm32h757-cm7` builds `examples_st.stm32h757i_eval_cm7` (green LED + trace), `stm32h757-cm4` builds `examples_st.stm32h757i_eval_cm4` (orange LED).
- The CM7 image of `examples/stm32h757i_eval/` also runs the display demo on the 800 x 480 DSI panel (OTM8009A set up by EMIL's `boards.mb1166`, frame buffers in AXI SRAM, pixel clock from PLL3 via `ConfigureLtdcClockEvalH757I`)
- Local vendor patches to keep on re-import: every `gcc/startup_*.s` (`Default_Handler_Forwarded`) and the `.cpu cortex-m4` copy `startup_stm32h757xx_cm4.s`; `hal_conf/stm32h7xx_hal_conf.h` (`hse_value`, `stm32_assert.h`, USART/COMP aliases); pin-data XML namespace rewritten to `http://mcd.rou.st.com/modules.php?name=mcu`; `GeneratePinoutTableStructure.xsl` skips `*_C` analog pads.
- Local vendor patches to keep on re-import (display): `USE_HAL_LTDC_REGISTER_CALLBACKS` and `USE_HAL_DMA2D_REGISTER_CALLBACKS` set to `1U` in `hal_conf/stm32{f4,f7,h7}xx_hal_conf.h`, because `LtdcStm` and `Dma2dStm` register their callbacks

## DCMI camera capture (`DcmiStm`)

- `DcmiStm` implements EMIL `hal::Camera` on F4, F7, H5 and H7, the families that have a DCMI; it compiles to nothing where `HAS_PERIPHERAL_DCMI` is undefined. On H5 the DCMI shares its interrupt and clock with PSSI. Its pins are one `MultiGpioPinStm` (D0..Dn, HSYNC, VSYNC, PIXCLK, `PinConfigTypeStm::dcmi`); XCLK for the sensor is the application's job (timer or MCO).
- It owns a raw `DMA_HandleTypeDef` and calls `HAL_DCMI_Start_DMA` instead of using `DmaStm`: `DmaStm` takes `uint16_t` byte counts (a QVGA RGB565 frame is 153600 bytes) and has no double-buffer mode, which the ST HAL uses to split frames of more than 65535 words. On H5 it builds the one-node, or circular two-node, GPDMA linked-list queue that `HAL_DCMI_Start_DMA` expects. It enables the DMA clocks and never disables them, and it does not reserve its stream in `DmaStm`; a conflict with another user of the stream shows up as a second registration of the same IRQ.
- Both its vectors (DCMI and DMA stream) use `ImmediateInterruptHandler` at the same priority: the HAL re-programs the memory addresses of a split frame from the DMA ISR, so deferring that work to the event dispatcher would corrupt frames silently. The HAL callbacks only post an event; `onFrame` and `onError` run on the event dispatcher.
- It registers the HAL frame and error callbacks, so `USE_HAL_DCMI_REGISTER_CALLBACKS` is `1U` in the F4, F7, H5 and H7 `hal_conf` headers (keep on re-import).
- Frame buffers are caller-owned: 4-byte aligned, a whole number of words, at most 262140 bytes per DMA pass (65535 on H5) unless the frame divides evenly into a power-of-two number of passes, and not in TCM. A JPEG frame is snapshot-only and must fit in one pass. On F7/H7 with the D-cache enabled they must be 32-byte aligned and sized; the driver cleans and invalidates the cache around a capture.

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

Other target presets: `stm32wb55`, `stm32g070`, `stm32g431`, `stm32f429`, `stm32f746`, `stm32f767`, `stm32g474`, `stm32wba52`, `stm32wba55`, `stm32wba65`, `stm32h563`, `stm32h573`, `stm32h757-cm7`, `stm32h757-cm4`.

Validation firmware (stm32wb55, stm32wba55): `cmake --build --preset stm32wb55-RelWithDebInfo --target hal_st.validation_firmware`.

## Assistant behavior — be terse

- Minimal prose. No preamble/postamble, no restating the plan, no summaries unless asked
- Report results as file paths + build pass/fail (no test suite to report)
- Don't re-read files already read; batch reads; prefer targeted edits
