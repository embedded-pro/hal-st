#include "validation/firmware/AnalogInputGroup.hpp"
#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/firmware/AdcFactory.hpp"
#include "validation/firmware/Owners.hpp"
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

        // AdcTriggeredByTimerWithDma paces its conversions with TIM2 whatever the caller wants
        constexpr uint8_t burstTimer = 2;
        constexpr uint32_t vddaMillivolts = 3300;
        constexpr uint32_t maximumIndex = std::numeric_limits<uint16_t>::max();
        constexpr uint32_t minimumRate = 10;
        constexpr uint32_t maximumRate = 100000;
        constexpr uint32_t maximumRepeat = 2;
        constexpr std::size_t maximumListed = 64;
        constexpr infra::Duration readTimeout = std::chrono::milliseconds(1000);
        constexpr uint32_t burstMarginMs = 1000;

        constexpr std::array<const char*, 4> burstKeys{ { "n", "rate", "repeat", "out" } };

        static_assert(board::adcDma.dma == 1);
    }

    AnalogInputCommands::AnalogInputCommands(services::HilContext& context, const services::HilPinNaming& naming, hal::DmaStm& dma, TimerAllocation& timers, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , naming(naming)
        , dma(dma)
        , timers(timers)
        , resources(resources)
        , pins(context.pins, owners::analogInput)
        , pending(context.response)
        , commands{ {
              services::HilBind<AnalogInputCommands, &AnalogInputCommands::Read>("ain.read", "<adc> <pin|temp> [sampling=]", *this, context.response),
              services::HilBind<AnalogInputCommands, &AnalogInputCommands::Burst>("ain.burst", "<adc> <pin> n= rate= [repeat=] [out=]", *this, context.response),
          } }
    {}

    infra::MemoryRange<const AnalogInputCommands::Command> AnalogInputCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus AnalogInputCommands::Read(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, { "sampling" }))
            return HilStatus::usage;

        uint32_t index = 0;
        uint32_t samplingTime = board::adcDefaultSamplingTime;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 0, maximumIndex, status);
        arguments.Select("sampling", samplingTime, board::adcSamplingTimes, status);
        if (status != HilStatus::done)
            return status;

        if (index != board::adc)
            return HilStatus::range;

        const bool internal = arguments.Positional(1) == "temp";
        std::optional<HilPinId> pin;
        if (!internal)
        {
            status = ParseAnalogPin(arguments.Positional(1), pin.emplace());
            if (status != HilStatus::done)
                return status;
        }

        if (pending.Busy())
            return HilStatus::busy;

        status = Claim(pin, false);
        if (status != HilStatus::done)
            return status;

        hal::detail::AdcStmChannelConfig config;
        config.samplingTime = samplingTime;
        temperature = internal;

        auto& converter = adc.emplace(board::adc);
        operation = pending.Start(readTimeout);

        const auto onDone = [this](infra::MemoryRange<uint16_t> samples)
        {
            code = samples.front();

            // The driver still runs the callback that delivered the sample
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    FinishRead();
                });
        };

        if (internal)
            reader.emplace<hal::AnalogToDigitalInternalTemperatureStm>(converter, config).Measure(1, onDone);
        else
            reader.emplace<hal::AnalogToDigitalPinImplStm>(PinOrDummy(claimedPin), converter, config).Measure(1, onDone);

        return HilStatus::done;
    }

    HilStatus AnalogInputCommands::Burst(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, infra::MakeRange(burstKeys)) || !arguments.Has("n") || !arguments.Has("rate"))
            return HilStatus::usage;

        static constexpr std::array<HilChoice<Output>, 2> outputs{ {
            { "list", Output::list },
            { "stats", Output::stats },
        } };

        BurstRequest parsed;
        uint32_t index = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 0, maximumIndex, status);
        arguments.Number("n", parsed.samples, 1, bufferSize, status);
        arguments.Number("rate", parsed.rate, minimumRate, maximumRate, status);
        arguments.Number("repeat", parsed.repeat, 1, maximumRepeat, status);
        arguments.Select("out", parsed.output, outputs, status);
        if (status != HilStatus::done)
            return status;

        if (index != board::adc || (parsed.output == Output::list && parsed.samples > maximumListed))
            return HilStatus::range;

        status = ParseAnalogPin(arguments.Positional(1), parsed.pin);
        if (status != HilStatus::done)
            return status;

        if (pending.Busy())
            return HilStatus::busy;

        status = Claim(parsed.pin, true);
        if (status != HilStatus::done)
            return status;

        request = parsed;
        completedRuns = 0;

        auto& converter = adc.emplace(board::adc);
        auto& receiveStream = stream.emplace(dma, hal::DmaChannelId(board::adcDma.dma, board::adcDma.channel, board::adcDmaRequest));
        burst.emplace(infra::MakeRange(buffer), converter, receiveStream, TriggerTiming(burstTimer, request.rate), PinOrDummy(claimedPin));

        const auto runMs = request.samples * 1000 / request.rate + 1;
        operation = pending.Start(std::chrono::milliseconds(request.repeat * runMs + burstMarginMs));
        StartRun();
        return HilStatus::done;
    }

    HilStatus AnalogInputCommands::ParseAnalogPin(infra::BoundedConstString text, HilPinId& pin) const
    {
        const auto parsed = services::HilArguments::ParsePin(text, naming);
        if (!parsed || !SupportsAnalog(*parsed))
            return HilStatus::pin;

        pin = *parsed;
        return HilStatus::done;
    }

    HilStatus AnalogInputCommands::Claim(const std::optional<HilPinId>& pin, bool timed)
    {
        HilStatus status = resources.Claim(Resource::adc, board::adc, owners::analogInput);
        if (status == HilStatus::done && timed)
            status = resources.Claim(Resource::dma1, board::adcDma.channel, owners::analogInput);
        if (status == HilStatus::done && timed)
            status = timers.Claim(burstTimer, owners::analogInput);

        claimedPin = nullptr;
        if (status == HilStatus::done && pin)
            status = pins.ClaimAnalog(*pin, claimedPin);

        if (status != HilStatus::done)
            Release();

        return status;
    }

    void AnalogInputCommands::Release()
    {
        pins.Release();
        claimedPin = nullptr;
        timers.Release(burstTimer, owners::analogInput);
        resources.Release(Resource::dma1, board::adcDma.channel, owners::analogInput);
        resources.Release(Resource::adc, board::adc, owners::analogInput);
    }

    void AnalogInputCommands::FinishRead()
    {
        reader.emplace<std::monostate>();
        adc.reset();
        Release();

        if (!pending.Complete(operation))
            return;

        auto line = context.response.Ok();
        line << " code=" << static_cast<uint32_t>(code);

        if (temperature)
        {
            const int32_t celsius = __LL_ADC_CALC_TEMPERATURE(vddaMillivolts, code, LL_ADC_RESOLUTION_12B);
            line << " mcelsius=" << (celsius < 0 ? "-" : "") << static_cast<uint32_t>(celsius < 0 ? -celsius : celsius) * 1000;
        }
    }

    void AnalogInputCommands::StartRun()
    {
        stopwatch.Start();
        burst->Measure(request.samples, [this](infra::MemoryRange<uint16_t> samples)
            {
                RunDone(samples);
            });
    }

    void AnalogInputCommands::RunDone(infra::MemoryRange<uint16_t> samples)
    {
        const auto microseconds = stopwatch.ElapsedUs();

        if (++completedRuns < request.repeat)
        {
            {
                auto line = context.response.Event("ain");
                line << " index=" << static_cast<uint32_t>(board::adc) << " run=" << completedRuns;
                PrintRun(line, samples, microseconds);
            }

            StartRun();
            return;
        }

        lastSamples = samples;
        lastMicroseconds = microseconds;

        // The driver still runs the callback that delivered the samples
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                FinishBurst();
            });
    }

    void AnalogInputCommands::FinishBurst()
    {
        burst.reset();
        stream.reset();
        adc.reset();
        Release();

        if (!pending.Complete(operation))
            return;

        auto line = context.response.Ok();
        PrintRun(line, lastSamples, lastMicroseconds);
    }

    void AnalogInputCommands::PrintRun(services::HilResponse::Line& line, infra::MemoryRange<const uint16_t> samples, uint32_t microseconds) const
    {
        if (request.output == Output::list)
        {
            line << " samples=";
            for (std::size_t i = 0; i != samples.size(); ++i)
                line << (i == 0 ? "" : ",") << static_cast<uint32_t>(samples[i]);
        }
        else
        {
            uint32_t sum = 0;
            for (auto sample : samples)
                sum += sample;

            const auto count = static_cast<uint32_t>(samples.size());
            const auto [minimum, maximum] = std::ranges::minmax(samples);
            line << " n=" << count << " min=" << static_cast<uint32_t>(minimum) << " max=" << static_cast<uint32_t>(maximum) << " mean=" << (sum + count / 2) / count;
        }

        line << " us=" << microseconds;
    }
}
