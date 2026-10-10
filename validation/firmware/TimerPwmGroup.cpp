#include "validation/firmware/TimerPwmGroup.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "infra/util/Tokenizer.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint32_t maximumPrescaler = 0xffff;

        constexpr std::array<const char*, 3> openKeys{ { "pins", "prescaler", "period" } };

        constexpr std::array<hal::PinConfigTypeStm, TimerPwmFactoryStm::maximumChannels> channelFunctions{ {
            hal::PinConfigTypeStm::timerChannel1,
            hal::PinConfigTypeStm::timerChannel2,
            hal::PinConfigTypeStm::timerChannel3,
            hal::PinConfigTypeStm::timerChannel4,
        } };

        constexpr std::array<uint32_t, TimerPwmFactoryStm::maximumChannels> timerChannels{ {
            TIM_CHANNEL_1,
            TIM_CHANNEL_2,
            TIM_CHANNEL_3,
            TIM_CHANNEL_4,
        } };

        constexpr PwmChannelGroupNames names{ "tpwm.open", "tpwm.duty", "tpwm.pulse", "tpwm.start", "tpwm.stop", "tpwm.close", " timclk=" };

        TIM_TypeDef* Instance(uint8_t timer)
        {
            return hal::peripheralTimer[timer - 1];
        }

        uint32_t MaximumPeriod(uint8_t timer)
        {
            return IS_TIM_32B_COUNTER_INSTANCE(Instance(timer)) ? 0xffffffffu : 0xffffu;
        }
    }

    HilStatus ParseChannelPins(const services::HilArguments& arguments, const services::HilPinNaming& naming, infra::MemoryRange<std::optional<HilPinId>> pins, std::size_t& count)
    {
        const auto text = arguments.Key("pins");
        if (!text)
            return HilStatus::usage;

        infra::Tokenizer tokens(*text, ',');
        count = tokens.Size();
        if (count == 0 || count > pins.size())
            return HilStatus::usage;

        bool used = false;
        for (std::size_t position = 0; position != count; ++position)
        {
            const auto token = tokens.Token(position);
            pins[position] = std::nullopt;
            if (token == "-")
                continue;

            pins[position] = services::HilArguments::ParsePin(token, naming);
            if (!pins[position])
                return HilStatus::pin;

            used = true;
        }

        return used ? HilStatus::done : HilStatus::usage;
    }

    PwmChannelGroup::PwmChannelGroup(services::HilContext& context, PwmChannelFactory& factory, services::HilOwner owner, const PwmChannelGroupNames& names)
        : services::HilSingleInstanceGroup(context, factory, owner)
        , factory(factory)
        , clockKey(names.clock)
        , commands{ {
              OpenCommand(names.open, "<index> pins=<pin|->[,<pin|->...] [key=value]..."),
              services::HilBind<PwmChannelGroup, &PwmChannelGroup::Duty>(names.duty, "<index> <channel> <percent>", *this, context.response),
              services::HilBind<PwmChannelGroup, &PwmChannelGroup::Pulse>(names.pulse, "<index> <channel> <on> <period>", *this, context.response),
              services::HilBind<PwmChannelGroup, &PwmChannelGroup::Start>(names.start, "<index> [ch=<channel>]", *this, context.response),
              services::HilBind<PwmChannelGroup, &PwmChannelGroup::Stop>(names.stop, "<index> [ch=<channel>]", *this, context.response),
              CloseCommand(names.close, "<index>"),
          } }
    {}

    infra::MemoryRange<const PwmChannelGroup::Command> PwmChannelGroup::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus PwmChannelGroup::OpenInstance(uint8_t index, const services::HilArguments& arguments)
    {
        return factory.Open(index, arguments, Pins());
    }

    void PwmChannelGroup::Opened(services::HilResponse::Line& line) const
    {
        line << clockKey << factory.Clock();
    }

    void PwmChannelGroup::CloseInstance()
    {}

    HilStatus PwmChannelGroup::Duty(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(3, 3, {}))
            return HilStatus::usage;

        uint8_t channel = 0;
        HilStatus status = FindChannel(arguments, 1, channel);

        uint32_t percent = 0;
        arguments.NumberAt(2, percent, 0, 100, status);
        if (status != HilStatus::done)
            return status;

        factory.SetDuty(channel, static_cast<uint8_t>(percent));
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus PwmChannelGroup::Pulse(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(4, 4, {}))
            return HilStatus::usage;

        uint8_t channel = 0;
        HilStatus status = FindChannel(arguments, 1, channel);

        uint32_t pulseOn = 0;
        uint32_t period = 0;
        if (status == HilStatus::done)
        {
            arguments.NumberAt(2, pulseOn, 0, factory.MaximumCount(), status);
            arguments.NumberAt(3, period, 1, factory.MaximumCount(), status);
        }

        if (status != HilStatus::done)
            return status;

        factory.SetPulse(channel, pulseOn, period);
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus PwmChannelGroup::Start(const services::HilArguments& arguments)
    {
        std::optional<uint8_t> channel;
        HilStatus status = FindChannels(arguments, channel);
        if (status != HilStatus::done)
            return status;

        factory.Start(channel);
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus PwmChannelGroup::Stop(const services::HilArguments& arguments)
    {
        std::optional<uint8_t> channel;
        HilStatus status = FindChannels(arguments, channel);
        if (status != HilStatus::done)
            return status;

        factory.Stop(channel);
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus PwmChannelGroup::FindChannel(const services::HilArguments& arguments, std::size_t positional, uint8_t& channel) const
    {
        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        uint32_t number = 0;
        arguments.NumberAt(positional, number, 1, static_cast<uint32_t>(factory.Channels()), status);
        channel = static_cast<uint8_t>(number);
        return status;
    }

    HilStatus PwmChannelGroup::FindChannels(const services::HilArguments& arguments, std::optional<uint8_t>& channel) const
    {
        static constexpr std::array<const char*, 1> keys{ { "ch" } };
        if (!arguments.Shape(1, 1, infra::MakeRange(keys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done || !arguments.Has("ch"))
            return status;

        uint32_t number = 0;
        arguments.Number("ch", number, 1, static_cast<uint32_t>(factory.Channels()), status);
        channel = static_cast<uint8_t>(number);
        return status;
    }

    TimerPwmFactoryStm::TimerPwmFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers)
        : naming(naming)
        , timers(timers)
    {}

    uint8_t TimerPwmFactoryStm::Instances() const
    {
        return board::timerInstances;
    }

    infra::MemoryRange<const char* const> TimerPwmFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus TimerPwmFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus TimerPwmFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        status = timers.Claim(index, owners::timerPwm);
        if (status != HilStatus::done)
            return status;

        status = Claim(index, request, pins);
        if (status != HilStatus::done)
        {
            timers.Release(index, owners::timerPwm);
            return status;
        }

        Construct(index, request);
        return HilStatus::done;
    }

    void TimerPwmFactoryStm::Close(uint8_t index, const infra::Function<void()>& onClosed)
    {
        run.Stop(std::nullopt);
        driver.emplace<std::monostate>();
        base = nullptr;
        timers.Release(index, owners::timerPwm);
        onClosed();
    }

    uint32_t TimerPwmFactoryStm::Clock() const
    {
        return clock;
    }

    std::size_t TimerPwmFactoryStm::Channels() const
    {
        return channels;
    }

    uint32_t TimerPwmFactoryStm::MaximumCount() const
    {
        return MaximumPeriod(timer);
    }

    void TimerPwmFactoryStm::SetDuty(uint8_t channel, uint8_t percent)
    {
        base->Channel(channel).SetDuty(percent);
    }

    void TimerPwmFactoryStm::SetPulse(uint8_t channel, uint32_t pulseOn, uint32_t period)
    {
        base->Channel(channel).SetPulse(pulseOn, period);
    }

    void TimerPwmFactoryStm::Start(std::optional<uint8_t> channel)
    {
        run.Start(channel);
    }

    void TimerPwmFactoryStm::Stop(std::optional<uint8_t> channel)
    {
        run.Stop(channel);
    }

    HilStatus TimerPwmFactoryStm::Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const
    {
        if (!TimerExists(timer))
            return HilStatus::range;

        HilStatus status = HilStatus::done;
        arguments.Number("prescaler", request.prescaler, 0, maximumPrescaler, status);
        arguments.Number("period", request.period, 1, MaximumPeriod(timer), status);
        if (status != HilStatus::done)
            return status;

        status = ParseChannelPins(arguments, naming, infra::MakeRange(request.pins), request.channels);
        if (status != HilStatus::done)
            return status;

        for (std::size_t position = 0; position != request.channels; ++position)
            if (request.pins[position] && !SupportsFunction(*request.pins[position], channelFunctions[position], timer))
                return HilStatus::pin;

        for (std::size_t position = 0; position != request.channels; ++position)
            if (!IS_TIM_CCX_INSTANCE(Instance(timer), timerChannels[position]))
                return HilStatus::unsupported;

        return HilStatus::done;
    }

    HilStatus TimerPwmFactoryStm::Claim(uint8_t timer, const Request& request, services::HilPinOwner& pins) const
    {
        HilStatus status = HilStatus::done;

        for (std::size_t position = 0; position != request.channels && status == HilStatus::done; ++position)
        {
            hal::GpioPin* claimed = nullptr;
            status = pins.ClaimFunction(request.pins[position], Function(channelFunctions[position]), timer, claimed);
        }

        return status;
    }

    void TimerPwmFactoryStm::Construct(uint8_t timer, const Request& request)
    {
        this->timer = timer;
        clock = TimerClock(timer);
        channels = request.channels;

        for (std::size_t position = 0; position != request.channels; ++position)
        {
            if (request.pins[position])
                channelPins.Emplace(position, PortOf(*request.pins[position]), request.pins[position]->index);
            else
                channelPins.EmplaceUnused(position);
        }

        const hal::TimerBaseStm::Timing timing{ request.prescaler, request.period };
        const auto pins = channelPins.Range(request.channels);

        switch (request.channels)
        {
            case 1:
                base = &driver.emplace<hal::TimerPwmWithChannels<1>>(timer, timing, pins);
                break;
            case 2:
                base = &driver.emplace<hal::TimerPwmWithChannels<2>>(timer, timing, pins);
                break;
            case 3:
                base = &driver.emplace<hal::TimerPwmWithChannels<3>>(timer, timing, pins);
                break;
            default:
                base = &driver.emplace<hal::TimerPwmWithChannels<4>>(timer, timing, pins);
                break;
        }

        run.Attach(*base, request.channels);
    }

    TimerPwmGroup::TimerPwmGroup(services::HilContext& context, TimerPwmFactoryStm& factory)
        : PwmChannelGroup(context, factory, owners::timerPwm, names)
    {}
}
