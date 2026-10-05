#pragma once

#if defined(STM32WB)

#include "infra/timer/Timer.hpp"
#include "services/hil/HilCommand.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <cstdint>

namespace validation
{
    class HsemCommands
        : public services::TerminalCommands
    {
    public:
        HsemCommands(services::HilContext& context, TimerAllocation& timers, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Take(const services::HilArguments& arguments);
        services::HilStatus Release(const services::HilArguments& arguments);
        services::HilStatus Status(const services::HilArguments& arguments);
        services::HilStatus Lock(const services::HilArguments& arguments);
        services::HilStatus Mine(const services::HilArguments& arguments);

        services::HilStatus ParseProcess(const services::HilArguments& arguments, uint32_t& semaphore, uint32_t& process) const;
        uint32_t Wait(uint32_t semaphore, uint32_t holdUs);

    private:
        services::HilContext& context;
        TimerAllocation& timers;
        ResourceAllocation& resources;
        infra::TimerSingleShot holdTimer;
        uint32_t heldSemaphore = 0;
        uint32_t heldProcess = 0;
        Stopwatch stopwatch;
        std::array<Command, 5> commands;
    };
}

#endif
