#include "validation/firmware/LpTimerPwmGroup.hpp"
#include "validation/firmware/LpTimerGroup.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_LPTIMER) && !defined(STM32WB) && !defined(STM32G4)

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint32_t maximumCount = 0xffff;
        constexpr uint32_t maximumPrescaler = 128;

        constexpr std::array<const char*, 3> openKeys{ { "pins", "prescaler", "period" } };

        constexpr std::array<hal::PinConfigTypeStm, LpTimerPwmFactoryStm::maximumChannels> channelFunctions{ {
            hal::PinConfigTypeStm::lpTimerChannel1,
            hal::PinConfigTypeStm::lpTimerChannel2,
        } };

        constexpr std::array<uint32_t, LpTimerPwmFactoryStm::maximumChannels> lpTimerChannels{ {
            LPTIM_CHANNEL_1,
            LPTIM_CHANNEL_2,
        } };

        constexpr PwmChannelGroupNames names{ "lptpwm.open", "lptpwm.duty", "lptpwm.pulse", "lptpwm.start", "lptpwm.stop", "lptpwm.close", " lptimclk=" };
    }

    LpTimerPwmFactoryStm::LpTimerPwmFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources)
        : naming(naming)
        , resources(resources)
    {}

    uint8_t LpTimerPwmFactoryStm::Instances() const
    {
        return 3;
    }

    infra::MemoryRange<const char* const> LpTimerPwmFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus LpTimerPwmFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus LpTimerPwmFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        status = resources.Claim(Resource::lpTimer, index, owners::lpTimerPwm);
        if (status != HilStatus::done)
            return status;

        status = Claim(index, request, pins);
        if (status != HilStatus::done)
        {
            resources.Release(Resource::lpTimer, index, owners::lpTimerPwm);
            return status;
        }

        Construct(index, request);
        return HilStatus::done;
    }

    void LpTimerPwmFactoryStm::Close(uint8_t index, const infra::Function<void()>& onClosed)
    {
        run.Stop(std::nullopt);
        driver.emplace<std::monostate>();
        base = nullptr;
        resources.Release(Resource::lpTimer, index, owners::lpTimerPwm);
        onClosed();
    }

    uint32_t LpTimerPwmFactoryStm::Clock() const
    {
        return clock;
    }

    std::size_t LpTimerPwmFactoryStm::Channels() const
    {
        return channels;
    }

    uint32_t LpTimerPwmFactoryStm::MaximumCount() const
    {
        return maximumCount;
    }

    void LpTimerPwmFactoryStm::SetDuty(uint8_t channel, uint8_t percent)
    {
        base->Channel(channel).SetDuty(percent);
    }

    void LpTimerPwmFactoryStm::SetPulse(uint8_t channel, uint32_t pulseOn, uint32_t period)
    {
        base->Channel(channel).SetPulse(pulseOn, period);
    }

    void LpTimerPwmFactoryStm::Start(std::optional<uint8_t> channel)
    {
        run.Start(channel);
    }

    void LpTimerPwmFactoryStm::Stop(std::optional<uint8_t> channel)
    {
        run.Stop(channel);
    }

    HilStatus LpTimerPwmFactoryStm::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (!LpTimerExists(index))
            return HilStatus::range;

        HilStatus status = HilStatus::done;
        arguments.Number("prescaler", request.prescaler, 1, maximumPrescaler, status);
        arguments.Number("period", request.period, 1, maximumCount, status);
        if (status != HilStatus::done)
            return status;

        if (!LpTimerPrescaler(request.prescaler))
            return HilStatus::range;

        status = ParseChannelPins(arguments, naming, infra::MakeRange(request.pins), request.channels);
        if (status != HilStatus::done)
            return status;

        for (std::size_t position = 0; position != request.channels; ++position)
            if (request.pins[position] && !SupportsFunction(*request.pins[position], channelFunctions[position], index))
                return HilStatus::pin;

        for (std::size_t position = 0; position != request.channels; ++position)
            if (!IS_LPTIM_CCX_INSTANCE(hal::peripheralLpTimer[index - 1], lpTimerChannels[position]))
                return HilStatus::unsupported;

        return HilStatus::done;
    }

    HilStatus LpTimerPwmFactoryStm::Claim(uint8_t index, const Request& request, services::HilPinOwner& pins) const
    {
        HilStatus status = HilStatus::done;

        for (std::size_t position = 0; position != request.channels && status == HilStatus::done; ++position)
        {
            hal::GpioPin* claimed = nullptr;
            status = pins.ClaimFunction(request.pins[position], Function(channelFunctions[position]), index, claimed);
        }

        return status;
    }

    void LpTimerPwmFactoryStm::Construct(uint8_t index, const Request& request)
    {
        clock = LpTimerClock(index);
        channels = request.channels;

        for (std::size_t position = 0; position != request.channels; ++position)
        {
            if (request.pins[position])
                channelPins.Emplace(position, PortOf(*request.pins[position]), request.pins[position]->index);
            else
                channelPins.EmplaceUnused(position);
        }

        hal::LowPowerTimerBaseStm::Timing timing{};
        timing.period = request.period;
        timing.prescaler = *LpTimerPrescaler(request.prescaler);
        const auto pins = channelPins.Range(request.channels);

        if (request.channels == 1)
            base = &driver.emplace<hal::LpTimerPwmWithChannels<1>>(index, timing, pins);
        else
            base = &driver.emplace<hal::LpTimerPwmWithChannels<2>>(index, timing, pins);

        run.Attach(*base, request.channels);
    }

    LpTimerPwmGroup::LpTimerPwmGroup(services::HilContext& context, LpTimerPwmFactoryStm& factory)
        : PwmChannelGroup(context, factory, owners::lpTimerPwm, names)
    {}
}

#endif
