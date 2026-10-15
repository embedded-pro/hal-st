#pragma once

#if defined(STM32WB) || defined(STM32WBA)

#include "hal_st/stm32fxxx/PkaStm.hpp"
#include "infra/timer/Timer.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    class PkaCommands
        : public services::TerminalCommands
    {
    public:
        explicit PkaCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Multiply(const services::HilArguments& arguments);
        services::HilStatus CheckPoint(const services::HilArguments& arguments);
        services::HilStatus Compare(const services::HilArguments& arguments);
        services::HilStatus Reserve();
        uint32_t Start();
        const hal::PkaStm& Pka();

    private:
        services::HilContext& context;
        services::HilPendingOperation pending;
        std::optional<hal::PkaStm> pka;
        Stopwatch stopwatch;
        std::array<uint8_t, 32> scalar{};
        std::array<uint8_t, 32> x{};
        std::array<uint8_t, 32> y{};
        std::array<uint8_t, 60> a{};
        std::array<uint8_t, 60> b{};
        infra::TimePoint deadline;
        std::array<Command, 3> commands;
    };
}

#endif
