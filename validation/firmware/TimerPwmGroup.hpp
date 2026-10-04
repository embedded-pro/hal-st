#pragma once

#include "hal_st/stm32fxxx/TimerPwmStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilSingleInstanceGroup.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ChannelPins.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    services::HilStatus ParseChannelPins(const services::HilArguments& arguments, const services::HilPinNaming& naming, infra::MemoryRange<std::optional<HilPinId>> pins, std::size_t& count);

    // HAL_TIM_PWM_Start and HAL_LPTIM_PWM_Start refuse a channel that already runs, and the drivers assert on that
    template<class Base, std::size_t N>
    class PwmChannelRun
    {
    public:
        void Attach(Base& base, std::size_t channels);
        void Start(std::optional<uint8_t> channel);
        void Stop(std::optional<uint8_t> channel);

    private:
        void StartChannel(std::size_t position);

    private:
        Base* base = nullptr;
        std::size_t channels = 0;
        std::array<bool, N> running{};
        bool started = false;
    };

    class PwmChannelFactory
        : public services::HilInstanceFactory
    {
    protected:
        PwmChannelFactory() = default;
        PwmChannelFactory(const PwmChannelFactory& other) = delete;
        PwmChannelFactory& operator=(const PwmChannelFactory& other) = delete;
        ~PwmChannelFactory() = default;

    public:
        virtual services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins) = 0;
        virtual uint32_t Clock() const = 0;
        virtual std::size_t Channels() const = 0;
        virtual uint32_t MaximumCount() const = 0;
        virtual void SetDuty(uint8_t channel, uint8_t percent) = 0;
        virtual void SetPulse(uint8_t channel, uint32_t pulseOn, uint32_t period) = 0;
        virtual void Start(std::optional<uint8_t> channel) = 0;
        virtual void Stop(std::optional<uint8_t> channel) = 0;
    };

    struct PwmChannelGroupNames
    {
        const char* open;
        const char* duty;
        const char* pulse;
        const char* start;
        const char* stop;
        const char* close;
        const char* clock;
    };

    class PwmChannelGroup
        : public services::HilSingleInstanceGroup
    {
    public:
        PwmChannelGroup(services::HilContext& context, PwmChannelFactory& factory, services::HilOwner owner, const PwmChannelGroupNames& names);

        infra::MemoryRange<const Command> Commands() override;

    protected:
        services::HilStatus OpenInstance(uint8_t index, const services::HilArguments& arguments) override;
        void Opened(services::HilResponse::Line& line) const override;
        void CloseInstance() override;

    private:
        services::HilStatus Duty(const services::HilArguments& arguments);
        services::HilStatus Pulse(const services::HilArguments& arguments);
        services::HilStatus Start(const services::HilArguments& arguments);
        services::HilStatus Stop(const services::HilArguments& arguments);
        services::HilStatus FindChannel(const services::HilArguments& arguments, std::size_t positional, uint8_t& channel) const;
        services::HilStatus FindChannels(const services::HilArguments& arguments, std::optional<uint8_t>& channel) const;

    private:
        PwmChannelFactory& factory;
        const char* clockKey;
        std::array<Command, 6> commands;
    };

    class TimerPwmFactoryStm
        : public PwmChannelFactory
    {
    public:
        static constexpr std::size_t maximumChannels = 4;

        TimerPwmFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers);

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
            uint32_t prescaler = 0;
            uint32_t period = 6399;
            std::size_t channels = 0;
            std::array<std::optional<HilPinId>, maximumChannels> pins;
        };

        services::HilStatus Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(uint8_t timer, const Request& request, services::HilPinOwner& pins) const;
        void Construct(uint8_t timer, const Request& request);

    private:
        const services::HilPinNaming& naming;
        TimerAllocation& timers;
        ChannelPins<maximumChannels> channelPins;
        std::variant<std::monostate, hal::TimerPwmWithChannels<1>, hal::TimerPwmWithChannels<2>, hal::TimerPwmWithChannels<3>, hal::TimerPwmWithChannels<4>> driver;
        hal::TimerPwmBaseStm* base = nullptr;
        PwmChannelRun<hal::TimerPwmBaseStm, maximumChannels> run;
        uint8_t timer = 0;
        uint32_t clock = 0;
        std::size_t channels = 0;
    };

    class TimerPwmGroup
        : public PwmChannelGroup
    {
    public:
        TimerPwmGroup(services::HilContext& context, TimerPwmFactoryStm& factory);
    };

    ////    Implementation    ////

    template<class Base, std::size_t N>
    void PwmChannelRun<Base, N>::Attach(Base& base, std::size_t channels)
    {
        this->base = &base;
        this->channels = channels;
        running.fill(false);
        started = false;
    }

    template<class Base, std::size_t N>
    void PwmChannelRun<Base, N>::Start(std::optional<uint8_t> channel)
    {
        if (channel)
        {
            StartChannel(*channel - 1);
            return;
        }

        if (!started && std::none_of(running.begin(), running.begin() + channels, [](bool value)
                            {
                                return value;
                            }))
        {
            base->Start();
            started = true;
            std::fill_n(running.begin(), channels, true);
            return;
        }

        for (std::size_t position = 0; position != channels; ++position)
            StartChannel(position);
    }

    template<class Base, std::size_t N>
    void PwmChannelRun<Base, N>::Stop(std::optional<uint8_t> channel)
    {
        if (!channel)
        {
            base->Stop();
            started = false;
            running.fill(false);
            return;
        }

        if (running[*channel - 1])
        {
            base->Channel(*channel).Stop();
            running[*channel - 1] = false;
        }
    }

    template<class Base, std::size_t N>
    void PwmChannelRun<Base, N>::StartChannel(std::size_t position)
    {
        if (running[position])
            return;

        base->Channel(static_cast<uint8_t>(position + 1)).Start();
        running[position] = true;
    }
}
