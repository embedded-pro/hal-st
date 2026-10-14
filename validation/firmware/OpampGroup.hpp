#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_OPAMP)

#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/DigitalToAnalogPinStm.hpp"
#include "hal_st/stm32fxxx/OpampStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinPool.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class OpampCommands
        : public services::TerminalCommands
    {
    public:
        OpampCommands(services::HilContext& context, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Request
        {
            hal::OpampStm::Config config;
            std::optional<HilPinId> inputPlus;
            std::optional<HilPinId> inputMinus;
            bool pinOutput = false;
            uint16_t dacCode = 0;
        };

        struct Dac
        {
            Dac(uint8_t dac, uint8_t channel);

            hal::DacStm dac;
            hal::DigitalToAnalogPinImplStm output;
        };

        services::HilStatus Open(const services::HilArguments& arguments);
        services::HilStatus SetDac(const services::HilArguments& arguments);
        services::HilStatus Read(const services::HilArguments& arguments);
        services::HilStatus Close(const services::HilArguments& arguments);
        services::HilStatus Parse(const services::HilArguments& arguments, uint8_t index, Request& request) const;
        services::HilStatus Check(uint8_t index, Request& request) const;
        services::HilStatus Claim(uint8_t index, const Request& request);
        services::HilStatus Find(const services::HilArguments& arguments) const;
        void FinishRead();
        void Release();

    private:
        services::HilContext& context;
        ResourceAllocation& resources;
        services::HilPinOwner pins;
        services::HilPendingOperation pending;
        std::optional<uint8_t> openIndex;
        bool internalOutput = false;
        std::optional<uint8_t> dacResource;
        bool adcClaimed = false;
        hal::GpioPin* inputPlusPin = nullptr;
        hal::GpioPin* inputMinusPin = nullptr;
        hal::GpioPin* outputPin = nullptr;
        std::optional<Dac> dac;
        std::optional<hal::OpampStm> opamp;
        std::optional<hal::AdcStm> adc;
        std::variant<std::monostate, hal::AnalogToDigitalChannelStm, hal::AnalogToDigitalPinImplStm> reader;
        uint32_t operation = 0;
        uint16_t code = 0;
        std::array<Command, 4> commands;
    };
}

#endif
