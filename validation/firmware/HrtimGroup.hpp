#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_HRTIM)

#include "hal_st/stm32fxxx/HighResolutionTimerStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinPool.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    class HrtimCommands
        : public services::TerminalCommands
    {
    public:
        HrtimCommands(services::HilContext& context, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        enum class OutputPins : uint8_t
        {
            both,
            first,
            second
        };

        struct Request
        {
            hal::HighResolutionPwmStm::Config config;
            OutputPins outputPins = OutputPins::both;
            uint32_t frequency = 10000;
            std::optional<services::HilPinId> faultPin;
        };

        services::HilStatus Open(const services::HilArguments& arguments);
        services::HilStatus Duty(const services::HilArguments& arguments);
        services::HilStatus Phase(const services::HilArguments& arguments);
        services::HilStatus Frequency(const services::HilArguments& arguments);
        services::HilStatus Stop(const services::HilArguments& arguments);
        services::HilStatus Fault(const services::HilArguments& arguments);
        services::HilStatus Adc(const services::HilArguments& arguments);
        services::HilStatus Close(const services::HilArguments& arguments);
        services::HilStatus Parse(const services::HilArguments& arguments, hal::HighResolutionPwmStm::Unit unit, Request& request) const;
        services::HilStatus Claim(uint8_t index, const Request& request);
        services::HilStatus Find(const services::HilArguments& arguments) const;
        services::HilStatus Count(uint32_t trigger, uint32_t samples, uint32_t& elapsedUs);
        std::size_t Channels() const;
        void ReportTiming();
        void Release();

    private:
        services::HilContext& context;
        ResourceAllocation& resources;
        services::HilPinOwner pins;
        std::optional<uint8_t> openIndex;
        Request opened;
        hal::GpioPin* output1Pin = nullptr;
        hal::GpioPin* output2Pin = nullptr;
        hal::GpioPin* faultPin = nullptr;
        std::optional<hal::HighResolutionTimerStm> timer;
        std::optional<hal::HighResolutionPwmStm> pwm;
        std::array<Command, 8> commands;
    };
}

#endif
