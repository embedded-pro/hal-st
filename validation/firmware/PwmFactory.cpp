#include "validation/firmware/PwmFactory.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "infra/util/Tokenizer.hpp"
#include "services/hil/HilCommand.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;
        using Alignment = hal::PwmStmBase::Alignment;
        using TriggerOutput = hal::PwmStmBase::TriggerOutput;

        constexpr uint32_t maximumPrescaler = 0xffff;
        constexpr uint32_t maximumDeadTimeNs = 1000000;
        constexpr uint32_t maximumBreakFilter = 15;

        constexpr std::array<const char*, 17> openKeys{ { "channels", "pins", "freq", "mode", "prescaler", "dead", "inv", "invn", "idle", "idlen", "brk", "brkpol", "brkauto", "sync", "preload", "brkfilter", "trgo" } };

        constexpr std::array<HilChoice<Alignment>, 5> alignments{ {
            { "edge", Alignment::edgeAligned },
            { "edgedown", Alignment::edgeAlignedDownCounting },
            { "center", Alignment::centerAlignedDownCounting },
            { "centerup", Alignment::centerAlignedUpCounting },
            { "centerboth", Alignment::centerAlignedBothCounting },
        } };

        constexpr std::array<HilChoice<TriggerOutput>, 8> triggerOutputs{ {
            { "reset", TriggerOutput::reset },
            { "enable", TriggerOutput::enable },
            { "update", TriggerOutput::update },
            { "oc1", TriggerOutput::comparePulse },
            { "oc1ref", TriggerOutput::compareChannel1 },
            { "oc2ref", TriggerOutput::compareChannel2 },
            { "oc3ref", TriggerOutput::compareChannel3 },
            { "oc4ref", TriggerOutput::compareChannel4 },
        } };

        constexpr std::array<HilChoice<bool>, 2> breakPolarities{ {
            { "low", false },
            { "high", true },
        } };

        constexpr std::array<hal::PinConfigTypeStm, 4> channelFunctions{ {
            hal::PinConfigTypeStm::timerChannel1,
            hal::PinConfigTypeStm::timerChannel2,
            hal::PinConfigTypeStm::timerChannel3,
            hal::PinConfigTypeStm::timerChannel4,
        } };

        // hal-st has no function code beyond CH3N, so the driver cannot mux CH4N even where the timer has one
        constexpr std::array<hal::PinConfigTypeStm, 3> complementaryFunctions{ {
            hal::PinConfigTypeStm::timerChannel1N,
            hal::PinConfigTypeStm::timerChannel2N,
            hal::PinConfigTypeStm::timerChannel3N,
        } };

        constexpr std::array<uint32_t, 4> timerChannels{ {
            TIM_CHANNEL_1,
            TIM_CHANNEL_2,
            TIM_CHANNEL_3,
            TIM_CHANNEL_4,
        } };

        TIM_TypeDef* Instance(uint8_t timer)
        {
            return hal::peripheralTimer[timer - 1];
        }

        bool HasChannel(uint8_t timer, uint8_t channel)
        {
            return IS_TIM_CCX_INSTANCE(Instance(timer), timerChannels[channel - 1]);
        }

        bool HasComplementaryChannel(uint8_t timer, uint8_t channel)
        {
            return channel <= complementaryFunctions.size() && IS_TIM_CCXN_INSTANCE(Instance(timer), timerChannels[channel - 1]);
        }

        uint32_t CounterClock(uint8_t timer, uint32_t prescaler)
        {
            return TimerClock(timer) / (prescaler + 1);
        }

        bool IsCenterAligned(Alignment alignment)
        {
            return alignment != Alignment::edgeAligned && alignment != Alignment::edgeAlignedDownCounting;
        }

        bool ValidFrequency(uint8_t timer, uint32_t counterClock, bool centerAligned, uint32_t hertz)
        {
            const auto ticksPerPeriod = counterClock / hertz;
            if (ticksPerPeriod < (centerAligned ? 4u : 2u))
                return false;

            const uint32_t maximumCompare = IS_TIM_32B_COUNTER_INSTANCE(Instance(timer)) ? 0xffffffffu : 0xffffu;
            return (centerAligned ? ticksPerPeriod / 2 : ticksPerPeriod - 1) <= maximumCompare;
        }

        std::optional<uint8_t> ChannelOfPin(HilPinId pin, infra::MemoryRange<const hal::PinConfigTypeStm> functions, uint8_t timer)
        {
            for (std::size_t i = 0; i != functions.size(); ++i)
                if (SupportsFunction(pin, functions[i], timer))
                    return static_cast<uint8_t>(i + 1);

            return std::nullopt;
        }

        std::optional<std::optional<HilPinId>> ParseOptionalPin(infra::BoundedConstString text, const services::HilPinNaming& naming)
        {
            if (text == "-")
                return std::optional<HilPinId>();

            if (auto pin = services::HilArguments::ParsePin(text, naming))
                return std::optional<HilPinId>(*pin);

            return std::nullopt;
        }
    }

    PwmFactoryStm::Handle::Handle(Driver& driver, std::size_t channels)
        : driver(driver)
        , channels(channels)
    {}

    std::size_t PwmFactoryStm::Handle::Channels() const
    {
        return channels;
    }

    void PwmFactoryStm::Handle::Start(infra::MemoryRange<const hal::DutyCycle> dutyCycles)
    {
        really_assert(dutyCycles.size() == 1 || dutyCycles.size() == channels);

        std::array<hal::DutyCycle, maximumChannels> each;
        for (std::size_t i = 0; i != channels; ++i)
            each[i] = dutyCycles[dutyCycles.size() == 1 ? 0 : i];

        services::HilWithDriver(driver, [this, &each](auto& pwm)
            {
                switch (channels)
                {
                    case 1:
                        pwm.Start(each[0]);
                        break;
                    case 2:
                        pwm.Start(each[0], each[1]);
                        break;
                    case 3:
                        pwm.Start(each[0], each[1], each[2]);
                        break;
                    default:
                        pwm.Start(each[0], each[1], each[2], each[3]);
                        break;
                }
            });
    }

    void PwmFactoryStm::Handle::SetBaseFrequency(hal::Hertz baseFrequency)
    {
        services::HilWithDriver(driver, [baseFrequency](auto& pwm)
            {
                pwm.SetBaseFrequency(baseFrequency);
            });
    }

    void PwmFactoryStm::Handle::Stop()
    {
        services::HilWithDriver(driver, [](auto& pwm)
            {
                pwm.Stop();
            });
    }

    PwmFactoryStm::PwmFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers)
        : naming(naming)
        , timers(timers)
    {}

    uint8_t PwmFactoryStm::Instances() const
    {
        return board::timerInstances;
    }

    infra::MemoryRange<const char* const> PwmFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus PwmFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus PwmFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilPwmHandle*& opened)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        status = timers.Claim(index, services::HilOwners::pwm);
        if (status != HilStatus::done)
            return status;

        ClaimedPins claimed;
        status = Claim(index, request, pins, claimed);
        if (status != HilStatus::done)
        {
            timers.Release(index, services::HilOwners::pwm);
            return status;
        }

        timing = Timing{ CounterClock(index, request.prescaler), IsCenterAligned(request.alignment) };
        opened = &Construct(index, request, claimed);
        return HilStatus::done;
    }

    void PwmFactoryStm::ReportOpened(uint8_t, services::HilResponse::Line& line)
    {
        line << " pwmclk=" << timing->counterClock;
    }

    HilStatus PwmFactoryStm::ChangeFrequency(uint8_t index, uint32_t hertz)
    {
        if (!ValidFrequency(index, timing->counterClock, timing->centerAligned, hertz))
            return HilStatus::range;

        return HilStatus::done;
    }

    void PwmFactoryStm::Close(uint8_t index, const infra::Function<void()>& onClosed)
    {
        handle.reset();
        driver.emplace<std::monostate>();
        timing.reset();
        timers.Release(index, services::HilOwners::pwm);
        onClosed();
    }

    HilStatus PwmFactoryStm::Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const
    {
        if (!TimerExists(timer))
            return HilStatus::range;

        HilStatus status = Parse(arguments, request);
        if (status != HilStatus::done)
            return status;

        const bool complementary = std::ranges::any_of(request.outputs, [](const Output& output)
            {
                return output.complementaryPin.has_value();
            });

        if (request.alignment != Alignment::edgeAligned && !IS_TIM_COUNTER_MODE_SELECT_INSTANCE(Instance(timer)))
            return HilStatus::unsupported;

        if (request.triggerOutput && !IS_TIM_MASTER_INSTANCE(Instance(timer)))
            return HilStatus::unsupported;

        if ((complementary || request.deadTime || request.idleHigh || request.complementaryIdleHigh || request.breakPin) && !IS_TIM_BREAK_INSTANCE(Instance(timer)))
            return HilStatus::unsupported;

        if (request.breakFilter.value_or(0) != 0 && std::ranges::find(board::breakFilterTimers, timer) == board::breakFilterTimers.end())
            return HilStatus::unsupported;

        status = ResolveOutputs(timer, request);
        if (status != HilStatus::done)
            return status;

        if (request.breakPin && !SupportsFunction(*request.breakPin, hal::PinConfigTypeStm::timerBreak, timer))
            return HilStatus::pin;

        if (!ValidFrequency(timer, CounterClock(timer, request.prescaler), IsCenterAligned(request.alignment), request.frequency))
            return HilStatus::range;

        return HilStatus::done;
    }

    HilStatus PwmFactoryStm::Parse(const services::HilArguments& arguments, Request& request) const
    {
        HilStatus status = HilStatus::done;
        arguments.Number("freq", request.frequency, 1, std::numeric_limits<uint32_t>::max(), status);
        arguments.Select("mode", request.alignment, alignments, status);
        if (arguments.Has("trgo"))
            arguments.Select("trgo", request.triggerOutput.emplace(), triggerOutputs, status);
        arguments.Number("prescaler", request.prescaler, 0, maximumPrescaler, status);

        if (auto deadTime = arguments.Key("dead"); deadTime && *deadTime != "off")
        {
            uint32_t nanoseconds = 0;
            arguments.Number("dead", nanoseconds, 0, maximumDeadTimeNs, status);
            request.deadTime = nanoseconds;
        }

        arguments.Flag("inv", request.inverted, status);
        arguments.Flag("invn", request.complementaryInverted, status);
        arguments.Flag("idle", request.idleHigh, status);
        arguments.Flag("idlen", request.complementaryIdleHigh, status);
        arguments.Flag("brkauto", request.breakAutomaticOutput, status);
        arguments.Flag("sync", request.synchronous, status);
        arguments.Flag("preload", request.preload, status);
        arguments.Select("brkpol", request.breakActiveHigh, breakPolarities, status);

        if (arguments.Has("brkfilter"))
        {
            uint32_t filter = 0;
            arguments.Number("brkfilter", filter, 0, maximumBreakFilter, status);
            request.breakFilter = static_cast<uint8_t>(filter);
        }

        arguments.Pin("brk", naming, request.breakPin, status);
        if (status != HilStatus::done)
            return status;

        if (request.breakFilter && !request.breakPin)
            return HilStatus::usage;

        return ParseOutputs(arguments, request);
    }

    HilStatus PwmFactoryStm::ParseOutputs(const services::HilArguments& arguments, Request& request) const
    {
        const auto channels = arguments.Key("channels");
        const auto pins = arguments.Key("pins");
        if (!channels && !pins)
            return HilStatus::usage;

        std::array<Output, maximumChannels> parsed{};
        std::size_t channelCount = 0;
        std::size_t pinCount = 0;

        if (channels)
        {
            infra::Tokenizer tokens(*channels, ',');
            channelCount = tokens.Size();

            for (std::size_t i = 0; i != channelCount; ++i)
            {
                auto channel = services::HilArguments::ParseNumber(tokens.Token(i));
                if (!channel)
                    return HilStatus::usage;

                if (*channel < 1 || *channel > maximumChannels)
                    return HilStatus::range;

                if (i < parsed.size())
                    parsed[i].channel = static_cast<uint8_t>(*channel);
            }
        }

        if (pins)
        {
            infra::Tokenizer tokens(*pins, ',');
            pinCount = tokens.Size();

            for (std::size_t i = 0; i != pinCount; ++i)
            {
                Output output;
                HilStatus status = ParseOutput(tokens.Token(i), output);
                if (status != HilStatus::done)
                    return status;

                if (i < parsed.size())
                {
                    parsed[i].pin = output.pin;
                    parsed[i].complementaryPin = output.complementaryPin;
                }
            }
        }

        const auto count = channels ? channelCount : pinCount;
        if (count == 0 || count > maximumChannels || (channels && pins && channelCount != pinCount))
            return HilStatus::usage;

        for (std::size_t i = 0; i != count; ++i)
        {
            if (channels)
                for (std::size_t j = 0; j != i; ++j)
                    if (parsed[j].channel == parsed[i].channel)
                        return HilStatus::usage;

            request.outputs.push_back(parsed[i]);
        }

        return HilStatus::done;
    }

    HilStatus PwmFactoryStm::ParseOutput(infra::BoundedConstString text, Output& output) const
    {
        const auto separator = text.find(':');
        infra::BoundedConstString complementaryText("-");

        if (separator != infra::BoundedConstString::npos)
        {
            complementaryText = text.substr(separator + 1);
            if (complementaryText.find(':') != infra::BoundedConstString::npos)
                return HilStatus::usage;
        }

        auto pin = ParseOptionalPin(text.substr(0, separator), naming);
        auto complementaryPin = ParseOptionalPin(complementaryText, naming);
        if (!pin || !complementaryPin)
            return HilStatus::pin;

        if (!*pin && !*complementaryPin)
            return HilStatus::usage;

        output.pin = *pin;
        output.complementaryPin = *complementaryPin;
        return HilStatus::done;
    }

    HilStatus PwmFactoryStm::ResolveOutputs(uint8_t timer, Request& request) const
    {
        for (auto& output : request.outputs)
        {
            if (output.channel != 0)
                continue;

            auto channel = output.pin ? ChannelOfPin(*output.pin, infra::MakeRange(channelFunctions), timer) : ChannelOfPin(*output.complementaryPin, infra::MakeRange(complementaryFunctions), timer);
            if (!channel)
                return HilStatus::pin;

            output.channel = *channel;
        }

        for (std::size_t i = 0; i != request.outputs.size(); ++i)
            for (std::size_t j = 0; j != i; ++j)
                if (request.outputs[j].channel == request.outputs[i].channel)
                    return HilStatus::usage;

        for (auto& output : request.outputs)
        {
            if (!HasChannel(timer, output.channel) || (output.complementaryPin && !HasComplementaryChannel(timer, output.channel)))
                return HilStatus::unsupported;

            const auto function = channelFunctions[output.channel - 1];

            if (!output.pin && !output.complementaryPin)
            {
                output.pin = FindFunctionPin(function, timer);
                if (!output.pin)
                    return HilStatus::pin;
            }

            if (output.pin && !SupportsFunction(*output.pin, function, timer))
                return HilStatus::pin;

            if (output.complementaryPin && !SupportsFunction(*output.complementaryPin, complementaryFunctions[output.channel - 1], timer))
                return HilStatus::pin;
        }

        return HilStatus::done;
    }

    HilStatus PwmFactoryStm::Claim(uint8_t timer, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const
    {
        HilStatus status = HilStatus::done;

        for (std::size_t i = 0; i != request.outputs.size() && status == HilStatus::done; ++i)
        {
            const auto& output = request.outputs[i];
            status = pins.ClaimFunction(output.pin, Function(channelFunctions[output.channel - 1]), timer, claimed.pins[i]);
            if (status == HilStatus::done && output.complementaryPin)
                status = pins.ClaimFunction(*output.complementaryPin, Function(complementaryFunctions[output.channel - 1]), timer, claimed.complementaryPins[i]);
        }

        if (status == HilStatus::done && request.breakPin)
            status = pins.ClaimFunction(*request.breakPin, Function(hal::PinConfigTypeStm::timerBreak), timer, claimed.breakPin);

        return status;
    }

    services::HilPwmHandle& PwmFactoryStm::Construct(uint8_t timer, const Request& request, const ClaimedPins& claimed)
    {
        hal::PwmStmBase::Config config;
        config.alignment = request.alignment;
        config.prescaler = static_cast<uint16_t>(request.prescaler);
        config.preloadEnabled = request.preload;
        config.triggerOutput = request.triggerOutput;

        if (request.deadTime)
            config.deadTime.emplace().duration = std::chrono::nanoseconds(*request.deadTime);

        if (request.breakPin)
        {
            auto& breakInput = config.breakInput.emplace();
            breakInput.activeHigh = request.breakActiveHigh;
            breakInput.automaticOutputEnable = request.breakAutomaticOutput;
            breakInput.filter = request.breakFilter.value_or(0);
        }

        infra::BoundedVector<hal::PwmStmBase::ChannelConfig>::WithMaxSize<maximumChannels> channels;
        for (std::size_t i = 0; i != request.outputs.size(); ++i)
            channels.emplace_back(request.outputs[i].channel, PinOrDummy(claimed.pins[i]), PinOrDummy(claimed.complementaryPins[i]),
                request.inverted, request.complementaryInverted, request.idleHigh, request.complementaryIdleHigh);

        auto& breakPin = PinOrDummy(claimed.breakPin);
        if (request.synchronous)
            driver.emplace<hal::SynchronousPwmStm>(timer, infra::MakeRange(channels), breakPin, config);
        else
            driver.emplace<hal::PwmStm>(timer, infra::MakeRange(channels), breakPin, config);

        auto& opened = handle.emplace(driver, request.outputs.size());
        opened.SetBaseFrequency(hal::Hertz(request.frequency));
        return opened;
    }
}
