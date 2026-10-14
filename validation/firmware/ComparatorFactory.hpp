#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_COMP)

#include "hal_st/stm32fxxx/ComparatorStm.hpp"
#include "hal_st/stm32fxxx/DigitalToAnalogPinStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousComparatorStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilComparatorCommands.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class ComparatorFactoryStm
        : public services::HilComparatorFactory
    {
    public:
        ComparatorFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilComparatorHandle& handle) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus SetDac(uint8_t index, uint16_t code);

    private:
        enum class InputMinus : uint8_t
        {
            pin,
            vrefintQuarter,
            vrefintHalf,
            vrefintThreeQuarters,
            vrefint,
            dac,
        };

        struct Request
        {
            uint8_t inputPlus = 0;
            InputMinus inputMinus = InputMinus::vrefintHalf;
            uint8_t inputMinusPin = 0;
            uint16_t dacCode = 2048;
            uint8_t hysteresis = 0;
            bool inverted = false;
            std::optional<HilPinId> output;
            uint32_t blanking = COMP_BLANKINGSRC_NONE;
            bool blankingAvailable = true;
            bool synchronous = false;
            hal::ComparatorInterruptsStm::Type interruptType = hal::ComparatorInterruptsStm::Type::dispatched;
        };

        struct Dac
        {
            Dac(uint8_t dac, uint8_t channel);

            hal::DacStm dac;
            hal::DigitalToAnalogPinImplStm output;
        };

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus ParseInputMinus(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus ParseBlanking(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        hal::ComparatorBaseStm::Config Config(const Request& request) const;
        void Release();

    private:
        const services::HilPinNaming& naming;
        ResourceAllocation& resources;
        std::optional<uint8_t> openIndex;
        std::optional<uint8_t> dacResource;
        std::optional<Dac> dac;
        std::variant<std::monostate, hal::ComparatorStm, hal::SynchronousComparatorStm> driver;
    };

    class ComparatorExtensionCommands
        : public services::TerminalCommands
    {
    public:
        ComparatorExtensionCommands(services::HilContext& context, ComparatorFactoryStm& factory);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Dac(const services::HilArguments& arguments);

    private:
        services::HilContext& context;
        ComparatorFactoryStm& factory;
        std::array<Command, 1> commands;
    };
}

#endif
