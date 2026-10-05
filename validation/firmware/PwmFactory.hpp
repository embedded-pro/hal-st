#pragma once

#include "hal_st/stm32fxxx/PwmStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousPwmStm.hpp"
#include "infra/util/BoundedVector.hpp"
#include "services/hil/commands/HilPwmCommands.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class PwmFactoryStm
        : public services::HilPwmFactory
    {
    public:
        PwmFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilPwmHandle*& handle) override;
        void ReportOpened(uint8_t index, services::HilResponse::Line& line) override;
        services::HilStatus ChangeFrequency(uint8_t index, uint32_t hertz) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

    private:
        static constexpr std::size_t maximumChannels = services::HilPwmCommands::maximumChannels;

        using Driver = std::variant<std::monostate, hal::PwmStm, hal::SynchronousPwmStm>;

        // PwmStm::Start(duty) asserts one duty per channel, so a single duty is replicated here instead of going through HilPwmAdapter
        class Handle
            : public services::HilPwmHandle
        {
        public:
            Handle(Driver& driver, std::size_t channels);

            std::size_t Channels() const override;
            void Start(infra::MemoryRange<const hal::DutyCycle> dutyCycles) override;
            void SetBaseFrequency(hal::Hertz baseFrequency) override;
            void Stop() override;

        private:
            Driver& driver;
            std::size_t channels;
        };

        struct Output
        {
            uint8_t channel = 0;
            std::optional<HilPinId> pin;
            std::optional<HilPinId> complementaryPin;
        };

        struct Request
        {
            infra::BoundedVector<Output>::WithMaxSize<maximumChannels> outputs;
            uint32_t frequency = 10000;
            bool centerAligned = false;
            uint32_t prescaler = 0;
            std::optional<uint32_t> deadTime;
            bool inverted = false;
            bool complementaryInverted = false;
            bool idleHigh = false;
            bool complementaryIdleHigh = false;
            std::optional<HilPinId> breakPin;
            bool breakActiveHigh = true;
            bool breakAutomaticOutput = false;
            bool synchronous = false;
        };

        struct ClaimedPins
        {
            std::array<hal::GpioPin*, maximumChannels> pins{};
            std::array<hal::GpioPin*, maximumChannels> complementaryPins{};
            hal::GpioPin* breakPin = nullptr;
        };

        struct Timing
        {
            uint32_t counterClock;
            bool centerAligned;
        };

        services::HilStatus Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Parse(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus ParseOutputs(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus ParseOutput(infra::BoundedConstString text, Output& output) const;
        services::HilStatus ResolveOutputs(uint8_t timer, Request& request) const;
        services::HilStatus Claim(uint8_t timer, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const;
        services::HilPwmHandle& Construct(uint8_t timer, const Request& request, const ClaimedPins& claimed);

    private:
        const services::HilPinNaming& naming;
        TimerAllocation& timers;
        Driver driver;
        std::optional<Handle> handle;
        std::optional<Timing> timing;
    };
}
