#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal_st/stm32fxxx/LpTimerPwmStm.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ChannelPins.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/TimerPwmGroup.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#if defined(HAS_PERIPHERAL_LPTIMER) && !defined(STM32WB)

namespace validation
{
    class LpTimerPwmFactoryStm
        : public PwmChannelFactory
    {
    public:
        static constexpr std::size_t maximumChannels = 2;

        LpTimerPwmFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins) override;
        uint32_t Clock() const override;
        std::size_t Channels() const override;
        uint32_t MaximumCount() const override;
        void SetDuty(uint8_t channel, uint8_t percent) override;
        void SetPulse(uint8_t channel, uint32_t pulseOn, uint32_t period) override;
        void Start(std::optional<uint8_t> channel) override;
        void Stop(std::optional<uint8_t> channel) override;

    private:
        struct Request
        {
            uint32_t prescaler = 1;
            uint32_t period = 6399;
            std::size_t channels = 0;
            std::array<std::optional<HilPinId>, maximumChannels> pins;
        };

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(uint8_t index, const Request& request, services::HilPinOwner& pins) const;
        void Construct(uint8_t index, const Request& request);

    private:
        const services::HilPinNaming& naming;
        ResourceAllocation& resources;
        ChannelPins<maximumChannels> channelPins;
        std::variant<std::monostate, hal::LpTimerPwmWithChannels<1>, hal::LpTimerPwmWithChannels<2>> driver;
        hal::LpTimerPwmBaseStm* base = nullptr;
        PwmChannelRun<hal::LpTimerPwmBaseStm, maximumChannels> run;
        uint32_t clock = 0;
        std::size_t channels = 0;
    };

    class LpTimerPwmGroup
        : public PwmChannelGroup
    {
    public:
        LpTimerPwmGroup(services::HilContext& context, LpTimerPwmFactoryStm& factory);
    };
}

#endif
