#pragma once

#include "hal_st/synchronous_stm32fxxx/SynchronousQuadratureEncoderLpTimStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousQuadratureEncoderStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilQeiCommands.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class QeiFactoryStm
        : public services::HilQeiFactory
    {
    public:
        QeiFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, hal::SynchronousQuadratureEncoder*& encoder) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus ReadIndex(uint8_t index, bool& asserted) const;

    private:
#if defined(HAS_PERIPHERAL_LPTIMER)
        using Driver = std::variant<std::monostate, hal::SynchronousQuadratureEncoderStm, hal::SynchronousQuadratureEncoderLpTimStm>;
#else
        using Driver = std::variant<std::monostate, hal::SynchronousQuadratureEncoderStm>;
#endif

        struct Request
        {
            bool lowPower = false;
            std::optional<HilPinId> a;
            std::optional<HilPinId> b;
            std::optional<HilPinId> index;
            hal::SynchronousQuadratureEncoderStm::Config config;
        };

        struct ClaimedPins
        {
            hal::GpioPin* a = nullptr;
            hal::GpioPin* b = nullptr;
            hal::GpioPin* index = nullptr;
        };

        struct OpenedInstance
        {
            uint8_t timer;
            bool lowPower;
            bool hasIndex;
        };

        services::HilStatus Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus ParseSettings(uint8_t timer, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus ParsePins(uint8_t timer, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(uint8_t timer, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const;
        hal::SynchronousQuadratureEncoder& Construct(uint8_t timer, const Request& request, const ClaimedPins& claimed);
        void ReleaseTimer(uint8_t timer, bool lowPower);

    private:
        const services::HilPinNaming& naming;
        TimerAllocation& timers;
        Driver driver;
        std::optional<OpenedInstance> opened;
    };

    class QeiExtensionCommands
        : public services::TerminalCommands
    {
    public:
        QeiExtensionCommands(services::HilContext& context, QeiFactoryStm& factory);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Index(const services::HilArguments& arguments);

    private:
        services::HilContext& context;
        QeiFactoryStm& factory;
        std::array<Command, 1> commands;
    };
}
