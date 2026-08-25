hal-st — Claude Instructions
Canonical rules: AGENTS.md (shared with Copilot and sub-agents). C++ coding detail: .github/instructions/hal-st-cpp.instructions.md. Copilot agents: .github/agents/. Build presets: CMakePresets.json.

Essentials (full detail in AGENTS.md):

No heap — bounded containers / std::array / std::optional; no recursion in driver code. Applies repo-wide (this is an MCU HAL library).
STM32 HAL/LL — HAL_*/LL_* only, never raw registers; HAL_FOO_Init/DeInit in ctor/dtor (RAII); InterruptHandler/DispatchedInterruptHandler, never NVIC_EnableIRQ directly; PeripheralPinStm for AF pins; DMA_STREAM_BASED vs DMA_CHANNEL_BASED wrappers.
Driver Config — inner Config struct, mandatory constexpr Config() {}, oneBasedIndex convention, HAS_PERIPHERAL_xxx guards from generated PeripheralTable.hpp (never hand-edit generated/).
Style — Allman braces, 4-space, PascalCase types/methods, camelCase members. No comments except non-obvious why.
No tests — hal-st has no unit test suite; validation is on real hardware (Nucleo/Discovery, logic analyser) and integration_test/. Don't add unit tests for driver changes.
No exceptions — std::optional/status enums; interfaces virtual ~I() = default.
Be terse — minimal prose; report file paths + build pass/fail.
