#pragma once

#include "hal/interfaces/LowPowerMode.hpp"
#include "hal_st/stm32fxxx/LowPowerModeStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <atomic>
#include <cstdint>

namespace validation
{
    class LowPowerCommands
        : public services::TerminalCommands
    {
    public:
        LowPowerCommands(services::HilContext& context, TimerAllocation& timers);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Request
        {
            hal::PowerMode mode = hal::PowerMode::sleep;
            HilPinId wake;
            HilPinId marker;
            bool rising = true;
            uint32_t timeoutMs = 2000;
        };

        struct Outcome
        {
            bool woke = false;
            uint32_t us = 0;
            uint32_t sleeps = 0;
        };

        services::HilStatus Enter(const services::HilArguments& arguments);
        services::HilStatus Parse(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(const Request& request, hal::GpioPin*& wake, hal::GpioPin*& marker);
        Outcome Sleep(const Request& request, hal::GpioPin& marker);

    private:
        services::HilContext& context;
        TimerAllocation& timers;
        services::HilPinOwner pins;
        hal::LowPowerModeStm lowPower;
        std::atomic<uint32_t> restored{ 0 };
        std::atomic<bool> woke{ false };
        std::array<Command, 1> commands;
    };
}
