#pragma once

#if defined(STM32WB) || defined(STM32WBA)

#include "hal_st/synchronous_stm32fxxx/SynchronousAesStm.hpp"
#include "services/hil/HilCommand.hpp"
#include <array>
#include <cstdint>

namespace validation
{
    class AesCommands
        : public services::TerminalCommands
    {
    public:
        explicit AesCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Encrypt(const services::HilArguments& arguments);
        services::HilStatus Decrypt(const services::HilArguments& arguments);
        services::HilStatus Run(const services::HilArguments& arguments, bool decrypt);

    private:
        services::HilContext& context;
        std::array<uint8_t, 16> key{};
        alignas(uint32_t) std::array<uint8_t, 80> input{};
        alignas(uint32_t) std::array<uint8_t, 80> output{};
        std::array<Command, 2> commands;
    };
}

#endif
