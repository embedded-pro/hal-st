# hal-st — Agent Rules (canonical)

Single source of truth for **Claude, Copilot, and sub-agents**. `CLAUDE.md` points here. Detailed C++ coding rules: `.github/instructions/hal-st-cpp.instructions.md` (binding for all `*.hpp/*.cpp/*.h/*.c` changes). Copilot custom agents: `.github/agents/`. Build presets: `CMakePresets.json`.

hal-st is a Hardware Abstraction Layer for ST ARM Cortex-M microcontrollers (F4, F7, G0, G4, H5, WB, WBA families), implementing [embedded-infra-lib](https://github.com/embedded-pro/embedded-infra-lib) HAL interfaces over the STM32 HAL/LL library. It's a copy of [philips-software/amp-hal-st](https://github.com/philips-software/amp-hal-st).

## Architecture

- `hal_st/cortex/` — ARM Cortex-M core (`InterruptCortex`, `DataWatchpointAndTrace`)
- `hal_st/stm32fxxx/` — STM32 peripheral drivers (Uart, Can, Spi, Adc, Gpio, Dma, Timer, Flash, Ethernet, USB, …), split into `ip/` (peripheral IP blocks) and `mcu/` (family wiring)
- `hal_st/synchronous_stm32fxxx/` — Blocking driver variants (`SynchronousUart`, `SynchronousSpiMaster`, …)
- `hal_st/instantiations/` — Board event infrastructure (`StmEventInfrastructure`, `NucleoUi`, `DiscoveryUi`)
- `hal_st/default_init/` — Startup code and atomics shim
- `hal_st/middlewares/` — `STM32_WPAN`, `ble_middleware`
- `hal_st_lwip/` — lwIP network stack instantiations
- `st/` — CMSIS headers, STM32 HAL driver sources (per family), `hal_conf/`, `ldscripts/`
- `services/st_util/` — ST bootloader communicator services
- `integration_test/` — hardware-in-the-loop cucumber test rig (`pcb/`, `flasher/`, `tester/`, `tested/`, `runner/`, `logic/`)
- `examples/` — `blink`, `helloworld`, `sesame`, `freertos`

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
- DMA: `DMA_STREAM_BASED` (F4/F7) vs `DMA_CHANNEL_BASED` (G0/G4/WB/WBA/H5) — use `hal_st` DMA wrappers, not raw HAL DMA handles
- Naming: `FooStm` drivers, `SynchronousFooStm` blocking variants

## Style

- Allman braces, 4-space indent, `.clang-format` authoritative
- PascalCase types/methods, camelCase members/locals; `const`-correct on all observer/query methods
- `#pragma once` for new/modified headers; legacy `#ifndef` guards may stay untouched
- No C-style casts — `static_cast<>`; `reinterpret_cast<>` only where the HAL requires register/void-pointer casts
- **No comments** except non-obvious *why*. No `TODO`/`FIXME`/`HACK`, no commented-out code

## Interfaces & errors

- Interfaces = pure virtual; `virtual ~I() = default` — **never** `= 0` destructors
- No exceptions. `std::optional<T>` or status enums. `really_assert()` for preconditions
- No global mutable state — all state lives in driver class members

## Testing

No unit tests in this repo. hal-st is validated by manual testing on Nucleo/Discovery boards, logic-analyser/scope verification, and the `integration_test/` hardware-in-the-loop rig — not by GoogleTest suites. Don't add unit tests for new or changed drivers. (`services/st_util/test/` is a pre-existing exception gated behind `HALST_BUILD_TESTS`; leave it as-is, don't extend the pattern elsewhere.)

## Build

```bash
cmake --preset host && cmake --build --preset host-Debug   # host tooling/build check
cmake --preset stm32f407 && cmake --build --preset stm32f407-RelWithDebInfo   # embedded target
```

Other target presets: `stm32wb55`, `stm32g070`, `stm32g431`, `stm32f429`, `stm32f746`, `stm32f767`, `stm32g474`, `stm32wba52`, `stm32wba65`, `stm32h563`, `stm32h573`.

## Assistant behavior — be terse

- Minimal prose. No preamble/postamble, no restating the plan, no summaries unless asked
- Report results as file paths + build pass/fail (no test suite to report)
- Don't re-read files already read; batch reads; prefer targeted edits
