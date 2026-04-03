---
applyTo: "**/*.{hpp,cpp,h,c}"
---

# hal-st C++ Coding Rules

These rules apply to all source files in the **hal-st** repository — an `embedded-infra-lib` Hardware Abstraction Layer for STM32 ARM Cortex-M microcontrollers (F4, F7, G0, G4, H5, WB, WBA families).

## Memory Management — Absolute Restrictions

- **No heap allocation**: Never use `new`, `delete`, `malloc`, `free`, `std::make_unique`, or `std::make_shared`
- **No dynamic containers**: Replace standard library containers with `infra::Bounded*` equivalents:
  - `infra::BoundedVector` instead of `std::vector`
  - `infra::BoundedDeque` instead of `std::deque`
  - `infra::BoundedString` instead of `std::string`
- **All peripheral handles** (`xxx_HandleTypeDef`) must be **non-static member variables**, zero-initialized inline:
  ```cpp
  UART_HandleTypeDef uartHandle{};   // member, zero-initialized
  ```
- **No recursion** in driver code — stack depth must be statically bounded
- Callbacks stored in `infra::AutoResetFunction<void()>` (one-shot) or `infra::Function<void()>` (persistent)

## STM32 HAL Library API

- Use only `HAL_*` and `LL_*` functions; **never write to hardware registers via magic offsets**
- Include the correct device header using the `DEVICE_HEADER` macro, not a family-specific path:
  ```cpp
  #include DEVICE_HEADER   // resolves to stm32f4xx.h, stm32g0xx.h, etc.
  ```
- `HAL_FOO_Init` in constructor, `HAL_FOO_DeInit` + clock disable in destructor
- HAL callbacks registered with `HAL_FOO_RegisterCallback` where the HAL supports registration; avoid overriding weak global weak callback symbols
- Clock enable macros: `__HAL_RCC_XXX_CLK_ENABLE()` / `__HAL_RCC_XXX_CLK_DISABLE()`
- Use `__HAL_RCC_XXX_FORCE_RESET()` + `__HAL_RCC_XXX_RELEASE_RESET()` during destruction

## Interrupt Handler Base Classes

- **Single-vector peripherals** (UART, SPI, I2C, Timer, ADC): inherit `private InterruptHandler`
- **Multi-vector peripherals** (CAN TX/RX/Error, SDIO, ETH): use one `private DispatchedInterruptHandler` member **per interrupt vector**
- Never call `NVIC_EnableIRQ` directly — use the `InterruptHandler` or `DispatchedInterruptHandler` class
- `ImmediateInterruptHandler` is reserved for ISRs that bypass the normal dispatch queue — document why if used

## PeripheralPinStm RAII

- Every GPIO alternate function pin must be configured using a `PeripheralPinStm` member variable
- Never call `HAL_GPIO_Init` directly for alternate function signals
- Declare `PeripheralPinStm` members in the **same order** as their initialization in the constructor initializer list
- Construct GPIO pins before enabling the peripheral; destruct them after peripheral disable

## Config Inner Struct

Every driver class must declare an inner `Config` struct:
```cpp
struct Config
{
    constexpr Config() {}   // mandatory default constructor

    struct PinConfig { GpioPinStm::PinId pin; uint8_t alternateFunction; };
    PinConfig tx{ GpioPinStm::PinId::pa9, 7 };
    PinConfig rx{ GpioPinStm::PinId::pa10, 7 };
    uint32_t baudrate{ 115200 };
    uint32_t priority{ 0 };
};
```
- `constexpr Config() {}` is **mandatory** — enables aggregate-like initialization and constexpr contexts
- Group > 4 related fields into named sub-structs (separate `PinConfig`, `TimingConfig`, `DmaConfig`, etc.)
- All fields must have sensible default values

## oneBasedIndex Convention

- Peripheral indices are **1-based** to match ST's USART1, SPI2, etc. naming
- Parameter named `uint8_t oneBasedIndex` consistently
- Bounds must be asserted: `really_assert(oneBasedIndex >= 1 && oneBasedIndex <= FOO_COUNT);`
- Access `PeripheralTable` arrays as `fooTable[oneBasedIndex - 1]` — never `fooTable[oneBasedIndex]`

## HAS_PERIPHERAL_xxx Guards

- All family-specific peripheral presence must be guarded:
  ```cpp
  #if HAS_PERIPHERAL_USART3
      // code for USART3 support
  #endif
  ```
- Guards come from the generated `PeripheralTable.hpp` — do not define them manually
- Add `static_assert(fooIndex <= FOO_COUNT, "fooIndex out of range");` for runtime-indexed table access

## Generated Files — Never Edit

- `generated/stm32fxxx/PeripheralTable.hpp` is auto-generated from XML sources via XSL transform
- Pinout table files in `generated/` are auto-generated
- **Never hand-edit any file in `generated/`**
- To add a new peripheral or pin, edit the source `.xml` and regenerate using the provided XSL transform script

## DMA Architecture

DMA differs by MCU family — use the correct conditional:
- **Stream-based** (STM32F4xx, F7xx): `#ifdef DMA_STREAM_BASED`, `DmaChannelId::stream`
- **Channel-based** (STM32G0xx, G4xx, WBxx, WBAxx, H5xx): `#ifdef DMA_CHANNEL_BASED`, `DmaChannelId::channel`

Use the appropriate `hal_st` DMA wrappers (`TransmitDmaChannel`, `ReceiveDmaChannel`, `TransceiverDmaChannel`, or circular variants) — do not create raw DMA HAL handles.

## Naming Conventions

- STM32-specific driver classes: `FooStm` suffix (e.g., `UartStm`, `SpiMasterStm`, `AdcStm`)
- Synchronous blocking variants: `SynchronousFooStm` in `hal_st/synchronous_stm32fxxx/`
- All state is `private` — no public member variables
- Allman brace style: opening brace on its own line
- PascalCase for types and methods; camelCase for member variables and local variables
- `const` on all non-mutating member functions
- `constexpr` for compile-time constants; `static constexpr` for class-level constants
- `#pragma once` as the only include guard

## C++ Language Rules

- No C-style casts — use `static_cast<>` for numeric conversions; `reinterpret_cast<>` only where the HAL requires casting register addresses or void pointers
- No implicit integer narrowing — cast explicitly and document why
- No global mutable state — all state is encapsulated in driver class members
- RAII: every resource acquired in a constructor is released in the corresponding destructor
- `const` correctness: all observer/query methods marked `const`
- Prefer `infra::MemoryRange<T>` over raw pointer + size pairs for buffer parameters

## No Automated Tests

There are no automated tests in this repository. Validation is performed by:
- Manual testing on Nucleo or Discovery boards
- Logic analyser / oscilloscope verification for timing-critical peripherals
- Integration with the higher-level `hal` interface tests in consuming projects
