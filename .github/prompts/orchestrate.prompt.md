---
description: "Start an orchestrated development workflow for hal-st: plan, implement, and review changes to STM32 peripheral drivers following all project conventions."
mode: agent
agent: "orchestrator"
model: "Claude Sonnet 4.6"
---

Start a new development workflow for a hal-st task.

Gather the following context from the user before routing:

1. **What needs to change?** (new peripheral driver, bug fix, new MCU family support, synchronous variant, BSP/instantiation)
2. **Which STM32 family?** (F4, F7, G0, G4, H5, WB, WBA — or multiple)
3. **Which peripheral?** (UART, SPI, I2C, CAN, ADC, Timer, DMA, GPIO, Ethernet, USB, …)
4. **DMA involved?** (and if so, is target family stream-based F4/F7 or channel-based G0/G4/WB/WBA/H5?)
5. **Synchronous or asynchronous?** (blocking poll vs event-driven `InterruptHandler`)
6. **Any existing driver to reference?** (identify the closest analog in `hal_st/stm32fxxx/`)

Use this information to triage and route to the appropriate specialist:
- **planner** — for new drivers, new family support, DMA integration, or multi-file architectural changes
- **executor** — for targeted bug fixes or small well-defined changes
- **reviewer** — for reviewing completed changes against hal-st project standards
