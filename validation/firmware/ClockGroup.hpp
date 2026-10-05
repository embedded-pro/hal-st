#pragma once

#include "services/hil/HilCommand.hpp"
#include <array>

namespace validation
{
    class ClockCommands
        : public services::TerminalCommands
    {
    public:
        explicit ClockCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Info(const services::HilArguments& arguments);
#if defined(STM32WB)
        services::HilStatus Mco(const services::HilArguments& arguments);
        services::HilStatus Hsi48(const services::HilArguments& arguments);
#endif

    private:
        services::HilContext& context;
#if defined(STM32WB)
        std::array<Command, 3> commands;
#else
        std::array<Command, 1> commands;
#endif
    };
}
