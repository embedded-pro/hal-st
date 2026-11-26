# hal-st — Agent Rules

<!-- shared:begin 0.7.0 sha256:000f44de0ff3a5bd -->
Canonical rules for Claude, Copilot and sub-agents. Sections marked *(shared)* come from embedded-pro/embedded-agent-kit:
edit them there, not here. *(repo)* sections belong to this repository. *Exceptions* overrides a shared rule.

## Behaviour *(shared)*

- **Be as terse as possible.** No preamble, summary or narration. Report file paths and pass/fail.
- Everything you write — code, docs, agent files, PR text — uses the fewest words that stay unambiguous.
- Prose (docs, comments, agent files, commit and PR text) follows ASD-STE100 Simplified Technical English:
  one word, one meaning; active voice; one instruction per sentence; sentences ≤ 20 words (procedures) or ≤ 25 (descriptions).
- Ambiguous request: ask. Clear request: act.
- Do only what was asked. No unrelated refactors, no extra features.
- Batch reads, don't re-read, prefer targeted edits.
- Never change a shared section. Add a deviation to *Exceptions* only when the user approves it.

## Workflow *(shared)*

1. Multi-file or architectural change: update the design document first. Get approval before you write code.
2. Read *Domain* and *Build*. Read each `.github/instructions/*.instructions.md` whose `applyTo` matches a file you change.
3. Find the nearest existing pattern. Follow it.
4. Per behaviour: write a failing test, write the minimum code that passes it, refactor with all tests green.
5. Add each new file to its `CMakeLists.txt`.
6. Update the documentation for each behavioural change.
7. Run the *Build* commands until build and tests pass. Never disable, skip or weaken a test to get green.
8. Non-trivial change: run the `reviewer` agent, or ask the user to run it. Fix each CRITICAL and WARNING finding.
9. Report changed files, build pass/fail, test pass/fail.

## Memory *(shared)*

Scope: production code. Tests and host tools may use the heap.

- Forbidden: `new`/`delete`/`malloc`/`free`, `make_unique`/`make_shared`,
  `std::vector`/`string`/`deque`/`list`/`map`/`set`, recursion.
- Use: `infra::BoundedVector<T>::WithMaxSize<N>`, `infra::BoundedString::WithStorage<N>`,
  `infra::BoundedDeque`, `infra::BoundedList`/`IntrusiveList`, `std::array`, `std::optional`, `std::variant`.

## Interfaces & errors *(shared)*

- No virtual destructors: each adds a deleting destructor and thunks to flash.
  Interface = pure virtual methods + `protected: ~I() = default;` (non-virtual).
- Public `virtual ~Base() = default;` only on a base destroyed through a base pointer:
  `delete`/`std::unique_ptr<Base>`, `infra::PolymorphicVariant<Base, ...>`, `infra::OptionalForPolymorphicObjects<Base, ...>`.
  Put it on that base, not on every interface.
- Not needed for `std::variant`, `std::optional<Concrete>`, `infra::SharedPtr`: they destroy the concrete type.
- No exceptions. `std::optional`, `bool` or status enum; `really_assert` for preconditions. Never swallow an error.
- Constructor injection; no global state.

## Optimisation *(shared)*

- No `#pragma GCC optimize`, no `optimize` attribute: GCC won't inline across differing options.
  The consumer sets flags per translation unit.
- `OPTIMIZE_FOR_SPEED` only on hot functions defined in a header.
- Hot paths: no heap, no virtual dispatch, no blocking.

## Style *(shared)*

- `.clang-format` is authoritative. Allman, 4 spaces, `{}` init.
- PascalCase types/methods; camelCase members and enum values; lowercase namespaces.
  No `I` prefix; implementations end in `Impl`.
- `#pragma once` only.
- Fixed-width integers. `const`/`constexpr`-correct. Functions ≤ 30 lines (hard limit 50).
- Comments: non-obvious *why* only, one line. No `TODO`/`FIXME`, commented-out code, or restating the code.
  Fix or delete a comment the change makes stale.

## Testing *(shared)*

- `TEST_F` for fixtures, `TYPED_TEST` for types, plain `TEST()` for stateless checks.
- `StrictMock` only. `EXPECT_NEAR` with explicit tolerance. Fixture in an anonymous namespace, macros outside.
- Heap allowed in tests.

## Documentation *(shared)*

- Update docs before or with any behavioural change.
- One fact, one place: link, never restate.
- Visuals: Mermaid or ASCII.
- Put each document in the folder for its kind: `theory/` (physics, mathematics, algorithms),
  `architecture/` (structure and deployment), `design/` (one component each),
  `verification_and_validation/` (test plans, test design, requirements).
- A test strategy or test plan is never a design document.
- Every document starts with front matter: `title`, `type`, `status`, `version`, `date`.
- A design document describes the component: responsibilities, behaviour, interfaces, states, limits.
  It names no MCU, board, vendor peripheral, register, clock frequency, class, function or file.
  Say "the target". Say "on the reference board" for a measured figure.
- Only an architecture document names the deployment target.
- A design document opens with `## Responsibilities`: one list of what the component does.
  No "not responsible for" list. Put boundaries in *Interfaces* (Required).
- No "Open Questions" section. Open an issue and link it. Put a known limitation in *Constraints & Limitations*.
- After a documentation change, build the rendered output if the repository has a build for it.
  Check that each diagram renders and that no table column runs into the next.
- Skill `doc-writer`, when installed: load it before you create or change a document.

## Desktop tools *(shared)*

Scope: host tools that people install and run, GUI or console.

- Open the log file first in `main`, before the application object.
  A GUI-subsystem build (`WIN32_EXECUTABLE`) has no stderr: the log file is its only output.
- Write the log to a per-user writable folder (`AppLocalDataLocation`) and rotate it.
  Capture the toolkit's messages (`qInstallMessageHandler`) from debug level.
- Install a crash handler after the log. It writes a `FATAL` line on a crash signal, `std::terminate`
  or an unhandled SEH exception, and a minidump on Windows. Qt tools use ui-cpp's `IssueLog` and `CrashReporter`.
- Log each start-up step: version, OS, toolkit version, arguments, restored settings, first frame.
- Provide `--safe-start`: it skips the saved state, so a bad saved state cannot block start-up.
- Give the log folder a menu item (Help ▸ Open Log Folder).
- Install test: start the tool, close it normally, then start it again with saved state.
  Print the log. On a failure, upload the log folder and the dumps.
<!-- shared:end -->

## Domain *(repo)*

- HAL for ST Cortex-M families F4, F7, G0, G4, H5, H7, WB, WBA. It implements embedded-infra-lib HAL interfaces
  over the STM32 HAL/LL library. It started as a copy of philips-software/amp-hal-st.
- Before you change a driver, read its section in `doc/driver-notes.md`.
- Layout:
  - `hal_st/stm32fxxx/` — drivers. `ip/` and `mcu/` hold the ST pin-data XML that generates `PeripheralTable`
    and `PinoutTableDefault`.
  - `hal_st/synchronous_stm32fxxx/` — blocking variants. `hal_st/instantiations/` — board event infrastructure.
  - `hal_st/bringup/` — startup glue; Cortex-M core code comes from EMIL `hal.cortex_m`.
  - `hal_st/middlewares/` — `STM32_WPAN`, `ble_middleware`. `hal_st_lwip/` — lwIP instantiations.
  - `st/` — vendor CMSIS, HAL sources, `hal_conf/`, `ldscripts/`. `services/st_util/` — ST bootloader communicator.
  - `validation/` — hardware-in-the-loop app: `firmware/` (`hal_st.validation_firmware`), `host/` (pytest and an
    Analog Discovery 3); commands in `validation/PROTOCOL.md`.
  - `examples/` (`HALST_BUILD_EXAMPLES`) and `demo/` (`HALST_BUILD_DEMOS`) — board examples and demos.
- *Memory* applies to the whole repository. Buffer parameters are `infra::MemoryRange<T>`, not pointer and size.
  Callbacks: `infra::AutoResetFunction<void()>` (one-shot) or `infra::Function<void()>`.
- Use only `HAL_*` and `LL_*`; never write a register through a magic offset. New code includes `DEVICE_HEADER`.
- RAII: `HAL_FOO_Init` and clock enable in the constructor; `HAL_FOO_DeInit`, force/release reset and clock disable
  in the destructor. Register HAL callbacks with `HAL_FOO_RegisterCallback`; do not override weak symbols.
- Peripheral handles (`xxx_HandleTypeDef`) are non-static members, value-initialised with `{}`.
- Interrupts: single vector → `private InterruptHandler`; several vectors → one `DispatchedInterruptHandler` member
  per vector. Never call `NVIC_EnableIRQ`. `ImmediateInterruptHandler` only with a one-line why.
- Each alternate-function pin is a `PeripheralPinStm` member, declared in constructor-initialiser order; never call
  `HAL_GPIO_Init` for it. Pins are constructed before the peripheral is enabled.
- Each driver has an inner `Config` with `constexpr Config() {}` and a default for every field. More than four related
  fields go in a named sub-struct (`PinConfig`, `DmaConfig`, ...).
- Peripheral indices are `uint8_t oneBasedIndex`: `really_assert` the range, read tables as `table[oneBasedIndex - 1]`.
- Family presence: `#if HAS_PERIPHERAL_xxx` from the generated `PeripheralTable.hpp`. Never edit `generated/`;
  change the XML and regenerate.
- DMA: `DMA_STREAM_BASED` (F4, F7, H7; on H7 the channel is the DMAMUX1 request) or `DMA_CHANNEL_BASED`
  (G0, G4, WB, WBA, H5). Use the `hal_st` DMA wrappers, not raw HAL DMA handles; exceptions are in
  `doc/driver-notes.md`.
- Names: `FooStm` drivers, `SynchronousFooStm` blocking variants. No public member variables.
- No C-style casts. `reinterpret_cast` only where the HAL needs a register or `void*` cast. No implicit narrowing.
- A change to a driver on WB55, WBA55 or G474 keeps `validation/PROTOCOL.md`, the firmware factory and the host
  tests in step.

## Build *(repo)*

```bash
cmake --preset host && cmake --build --preset host-Debug
cmake --preset stm32f407 && cmake --build --preset stm32f407-RelWithDebInfo
cmake --build --preset stm32wb55-RelWithDebInfo --target hal_st.validation_firmware
pytest validation/host/tests/unit
```

- Embedded presets build from a clean checkout and set `EMIL_INCLUDE_ECHO=Off`; `stm32f767` keeps Echo on and builds
  EMIL's Echo plugins under `<build>/_native_echo_plugins`.
- Presets: `stm32f407`, `stm32f429`, `stm32f746`, `stm32f746g-disco`, `stm32f767`, `stm32g070`, `stm32g431`,
  `stm32g474`, `stm32wb55`, `stm32wba52`, `stm32wba55`, `stm32wba65`, `stm32h563`, `stm32h573`, `stm32h745-cm7`,
  `stm32h745-cm4`, `stm32h757-cm7`, `stm32h757-cm4`.
- Validation firmware: `stm32wb55`, `stm32wba55`, `stm32g474`.

## Exceptions *(repo)*

- No unit tests: drivers are validated on hardware (`validation/`, boards, logic analyser). Workflow step 4 and
  *Testing* do not apply to drivers. `services/st_util/test/` is a legacy exception; do not extend it.
- Vendor code (`st/`, `hal_st/middlewares/`) and `generated/` keep their own style.
