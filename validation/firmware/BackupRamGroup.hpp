#pragma once

#include "hal/interfaces/BackupRam.hpp"
#include "services/hil/HilCommand.hpp"
#include <array>
#include <cstdint>

namespace validation
{
    class BackupRamCommands
        : public services::TerminalCommands
    {
    public:
        explicit BackupRamCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Info(const services::HilArguments& arguments);
        services::HilStatus Write(const services::HilArguments& arguments);
        services::HilStatus Read(const services::HilArguments& arguments);
        services::HilStatus Fill(const services::HilArguments& arguments);
        services::HilStatus Check(const services::HilArguments& arguments);

        services::HilStatus ParseIndex(const services::HilArguments& arguments, uint32_t& index) const;

    private:
        services::HilContext& context;
        hal::BackupRam<volatile uint32_t>& backupRam;
        std::array<Command, 5> commands;
    };
}
