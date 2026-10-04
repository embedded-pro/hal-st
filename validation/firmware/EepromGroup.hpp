#pragma once

#include "hal/interfaces/Eeprom.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilEepromCommands.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/I2cEepromStm.hpp"
#include "validation/firmware/I2cGroup.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    class EepromGroup
        : public services::TerminalCommands
        , public services::HilEepromFactory
    {
    public:
        EepromGroup(services::HilContext& context, const services::HilPinNaming& naming, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;
        hal::Eeprom& Instance() override;

    private:
        class DetachedEeprom
            : public hal::Eeprom
        {
        public:
            uint32_t Size() const override;
            void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
            void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
            void Erase(infra::Function<void()> onDone) override;
        };

        struct Request
        {
            I2cBus bus;
            I2cEepromStm::Config config;
            uint32_t timing = 0;
        };

        services::HilStatus Attach(const services::HilArguments& arguments);
        services::HilStatus Detach(const services::HilArguments& arguments);
        services::HilStatus Evaluate(const services::HilArguments& arguments, uint8_t& index, Request& request) const;
        void Error(I2cEepromError error, uint32_t address);
        void Destroy();

    private:
        services::HilContext& context;
        const services::HilPinNaming& naming;
        ResourceAllocation& resources;
        services::HilPinOwner pins;
        std::optional<I2cStmForHil> driver;
        std::optional<I2cEepromStm> adapter;
        DetachedEeprom detached;
        uint8_t index = 0;
        bool detaching = false;
        std::array<Command, 2> commands;
    };
}
