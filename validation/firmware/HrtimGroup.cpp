#include "validation/firmware/HrtimGroup.hpp"

#if defined(HAS_PERIPHERAL_HRTIM)

#include "BoardProfile.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include <chrono>
#include <limits>

namespace validation
{
    namespace
    {
        using services::HilStatus;
        using Pwm = hal::HighResolutionPwmStm;
        using Outputs = Pwm::Outputs;

        constexpr uint32_t units = 6;
        constexpr uint32_t maximumDeadTimeNs = 10'000;
        constexpr uint32_t minimumSamples = 2;
        constexpr uint32_t maximumSamples = 1000;
        constexpr uint32_t sampleTimeoutMs = 10;

        constexpr std::array<const char*, 8> openKeys{ { "out", "mode", "freq", "dead", "deadfall", "flt", "fltpol", "adc" } };

        constexpr std::array<services::HilChoice<Outputs>, 3> modes{ {
            { "inphase", Outputs::inPhase },
            { "comp", Outputs::complementary },
            { "shift", Outputs::shifted },
        } };

        constexpr std::array<services::HilChoice<bool>, 2> polarities{ {
            { "low", false },
            { "high", true },
        } };

        constexpr std::array<hal::PinConfigTypeStm, units> output1Functions{ {
            hal::PinConfigTypeStm::hrtimChA1,
            hal::PinConfigTypeStm::hrtimChB1,
            hal::PinConfigTypeStm::hrtimChC1,
            hal::PinConfigTypeStm::hrtimChD1,
            hal::PinConfigTypeStm::hrtimChE1,
            hal::PinConfigTypeStm::hrtimChF1,
        } };

        constexpr std::array<hal::PinConfigTypeStm, units> output2Functions{ {
            hal::PinConfigTypeStm::hrtimChA2,
            hal::PinConfigTypeStm::hrtimChB2,
            hal::PinConfigTypeStm::hrtimChC2,
            hal::PinConfigTypeStm::hrtimChD2,
            hal::PinConfigTypeStm::hrtimChE2,
            hal::PinConfigTypeStm::hrtimChF2,
        } };

        constexpr std::array<hal::PinConfigTypeStm, Pwm::faultInputs> faultFunctions{ {
            hal::PinConfigTypeStm::hrtimFlt1,
            hal::PinConfigTypeStm::hrtimFlt2,
            hal::PinConfigTypeStm::hrtimFlt3,
            hal::PinConfigTypeStm::hrtimFlt4,
            hal::PinConfigTypeStm::hrtimFlt5,
            hal::PinConfigTypeStm::hrtimFlt6,
        } };

        Pwm::Unit UnitOf(uint32_t index)
        {
            return static_cast<Pwm::Unit>(index - 1);
        }

        HilStatus ClaimFunction(services::HilPinOwner& pins, hal::PinConfigTypeStm function, hal::GpioPin*& pin)
        {
            auto id = FindFunctionPin(function, 1);
            if (!id)
                return HilStatus::unsupported;

            return pins.ClaimFunction(*id, Function(function), 1, pin);
        }

        std::optional<uint8_t> FaultInputOf(HilPinId pin)
        {
            for (uint8_t input = 1; input <= Pwm::faultInputs; ++input)
                if (SupportsFunction(pin, faultFunctions[input - 1], 1))
                    return input;

            return std::nullopt;
        }
    }

    HrtimCommands::HrtimCommands(services::HilContext& context, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , resources(resources)
        , pins(context.pins, owners::hrtim)
        , commands{ {
              services::HilBind<HrtimCommands, &HrtimCommands::Open>("hrtim.open", "<unit> [key=value]...", *this, context.response),
              services::HilBind<HrtimCommands, &HrtimCommands::Duty>("hrtim.duty", "<unit> <duty%>...", *this, context.response),
              services::HilBind<HrtimCommands, &HrtimCommands::Phase>("hrtim.phase", "<unit> <phase%>", *this, context.response),
              services::HilBind<HrtimCommands, &HrtimCommands::Frequency>("hrtim.freq", "<unit> <hz>", *this, context.response),
              services::HilBind<HrtimCommands, &HrtimCommands::Stop>("hrtim.stop", "<unit>", *this, context.response),
              services::HilBind<HrtimCommands, &HrtimCommands::Fault>("hrtim.fault", "<unit>", *this, context.response),
              services::HilBind<HrtimCommands, &HrtimCommands::Adc>("hrtim.adc", "<unit> <samples>", *this, context.response),
              services::HilBind<HrtimCommands, &HrtimCommands::Close>("hrtim.close", "<unit>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const HrtimCommands::Command> HrtimCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus HrtimCommands::Open(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(openKeys)))
            return HilStatus::usage;

        uint32_t index = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 1, units, status);
        if (status != HilStatus::done)
            return status;

        Request request;
        status = Parse(arguments, UnitOf(index), request);
        if (status != HilStatus::done)
            return status;

        if (openIndex)
            return HilStatus::busy;

        status = Claim(static_cast<uint8_t>(index), request);
        if (status != HilStatus::done)
        {
            Release();
            return status;
        }

        auto& highResolutionTimer = timer.emplace();
        if (request.config.faultInput)
            pwm.emplace(highResolutionTimer, UnitOf(index), PinOrDummy(output1Pin), PinOrDummy(output2Pin), PinOrDummy(faultPin), request.config);
        else
            pwm.emplace(highResolutionTimer, UnitOf(index), PinOrDummy(output1Pin), PinOrDummy(output2Pin), request.config);

        pwm->SetBaseFrequency(hal::Hertz(request.frequency));
        openIndex = static_cast<uint8_t>(index);
        opened = request;
        ReportTiming();
        return HilStatus::done;
    }

    HilStatus HrtimCommands::Duty(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 3, {}))
            return HilStatus::usage;

        HilStatus status = Find(arguments);
        if (status != HilStatus::done)
            return status;

        const auto count = arguments.PositionalCount() - 1;
        if (count != 1 && count != Channels())
            return HilStatus::usage;

        std::array<hal::DutyCycle, 2> dutyCycles{};
        for (std::size_t i = 0; i != count; ++i)
        {
            auto dutyCycle = services::HilArguments::ParseDutyCycle(arguments.Positional(i + 1));
            if (!dutyCycle)
                return HilStatus::usage;

            dutyCycles[i] = *dutyCycle;
        }

        if (count == 1)
            pwm->Start(dutyCycles[0]);
        else
            pwm->Start(dutyCycles[0], dutyCycles[1]);

        auto line = context.response.Ok();
        line << " compare=" << pwm->Compare(1);
        if (Channels() == 2)
            line << "," << pwm->Compare(2);

        return HilStatus::done;
    }

    HilStatus HrtimCommands::Phase(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, {}))
            return HilStatus::usage;

        HilStatus status = Find(arguments);
        if (status != HilStatus::done)
            return status;

        auto phase = services::HilArguments::ParseDutyCycle(arguments.Positional(1));
        if (!phase || opened.config.outputs != Outputs::shifted)
            return HilStatus::usage;

        if (phase->Value() >= hal::DutyCycle::fullScale)
            return HilStatus::range;

        pwm->SetPhase(*phase);
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus HrtimCommands::Frequency(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, {}))
            return HilStatus::usage;

        uint32_t frequency = 0;
        HilStatus status = Find(arguments);
        arguments.NumberAt(1, frequency, 1, std::numeric_limits<uint32_t>::max(), status);
        if (status != HilStatus::done)
            return status;

        if (!Pwm::TimingFor(hal::HighResolutionTimerStm::Clock(), hal::Hertz(frequency)))
            return HilStatus::range;

        pwm->SetBaseFrequency(hal::Hertz(frequency));
        ReportTiming();
        return HilStatus::done;
    }

    HilStatus HrtimCommands::Stop(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        auto status = Find(arguments);
        if (status != HilStatus::done)
            return status;

        pwm->Stop();
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus HrtimCommands::Fault(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        auto status = Find(arguments);
        if (status != HilStatus::done)
            return status;

        if (!opened.config.faultInput)
            return HilStatus::usage;

        context.response.Ok() << " active=" << (pwm->FaultActive() ? 1 : 0);
        return HilStatus::done;
    }

    HilStatus HrtimCommands::Adc(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, {}))
            return HilStatus::usage;

        uint32_t samples = 0;
        HilStatus status = Find(arguments);
        arguments.NumberAt(1, samples, minimumSamples, maximumSamples, status);
        if (status != HilStatus::done)
            return status;

        if (!opened.config.adcTrigger)
            return HilStatus::usage;

        auto trigger = board::hrtimAdcTriggers[*opened.config.adcTrigger - 1];
        if (!trigger)
            return HilStatus::unsupported;

        status = resources.Claim(Resource::adc, board::adc, owners::hrtim);
        if (status != HilStatus::done)
            return status;

        uint32_t elapsedUs = 0;
        status = Count(*trigger, samples, elapsedUs);
        resources.Release(Resource::adc, board::adc, owners::hrtim);
        if (status != HilStatus::done)
            return status;

        context.response.Ok() << " samples=" << samples << " us=" << elapsedUs;
        return HilStatus::done;
    }

    HilStatus HrtimCommands::Close(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        auto status = Find(arguments);
        if (status != HilStatus::done)
            return status;

        Release();
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus HrtimCommands::Parse(const services::HilArguments& arguments, Pwm::Unit unit, Request& request) const
    {
        static constexpr std::array<services::HilChoice<OutputPins>, 3> outputChoices{ {
            { "both", OutputPins::both },
            { "1", OutputPins::first },
            { "2", OutputPins::second },
        } };

        auto& config = request.config;
        uint32_t rising = 0;
        uint32_t falling = 0;
        bool faultActiveHigh = false;
        uint32_t adcTrigger = 0;
        HilStatus status = HilStatus::done;
        arguments.Select("out", request.outputPins, outputChoices, status);
        arguments.Select("mode", config.outputs, modes, status);
        arguments.Number("freq", request.frequency, 1, std::numeric_limits<uint32_t>::max(), status);
        arguments.Number("dead", rising, 0, maximumDeadTimeNs, status);
        arguments.Number("deadfall", falling, 0, maximumDeadTimeNs, status);
        arguments.Pin("flt", context.naming, request.faultPin, status);
        arguments.Select("fltpol", faultActiveHigh, polarities, status);
        arguments.Number("adc", adcTrigger, 1, Pwm::adcTriggers, status);
        if (status != HilStatus::done)
            return status;

        const bool deadTime = arguments.Has("dead") || arguments.Has("deadfall");
        if (deadTime && config.outputs != Outputs::complementary)
            return HilStatus::usage;

        if (arguments.Has("fltpol") && !request.faultPin)
            return HilStatus::usage;

        if (request.faultPin)
        {
            auto input = FaultInputOf(*request.faultPin);
            if (!input)
                return HilStatus::pin;

            auto& faultInput = config.faultInput.emplace();
            faultInput.input = *input;
            faultInput.activeHigh = faultActiveHigh;
        }

        if (!Pwm::TimingFor(hal::HighResolutionTimerStm::Clock(), hal::Hertz(request.frequency)))
            return HilStatus::range;

        if (adcTrigger != 0 && !Pwm::AdcTriggerAvailable(unit, static_cast<uint8_t>(adcTrigger)))
            return HilStatus::unsupported;

        if (deadTime)
        {
            auto& dead = config.deadTime.emplace();
            dead.rising = std::chrono::nanoseconds(rising);
            dead.falling = std::chrono::nanoseconds(arguments.Has("deadfall") ? falling : rising);
        }

        if (adcTrigger != 0)
            config.adcTrigger = static_cast<uint8_t>(adcTrigger);

        return HilStatus::done;
    }

    HilStatus HrtimCommands::Claim(uint8_t index, const Request& request)
    {
        HilStatus status = HilStatus::done;
        if (request.outputPins != OutputPins::second)
            status = ClaimFunction(pins, output1Functions[index - 1], output1Pin);
        if (status == HilStatus::done && request.outputPins != OutputPins::first)
            status = ClaimFunction(pins, output2Functions[index - 1], output2Pin);
        if (status == HilStatus::done && request.config.faultInput)
            status = pins.ClaimFunction(*request.faultPin, Function(faultFunctions[request.config.faultInput->input - 1]), 1, faultPin);

        return status;
    }

    HilStatus HrtimCommands::Find(const services::HilArguments& arguments) const
    {
        uint32_t index = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 1, units, status);
        if (status == HilStatus::done && openIndex != index)
            return HilStatus::notOpen;

        return status;
    }

    // Polled rather than through AdcStm: its drivers only know software and timer triggers
    HilStatus HrtimCommands::Count(uint32_t trigger, uint32_t samples, uint32_t& elapsedUs)
    {
        hal::AdcStm converter(board::adc);
        auto& handle = converter.Handle();

        ADC_ChannelConfTypeDef channel{};
        channel.Channel = ADC_CHANNEL_VREFINT;
        channel.Rank = ADC_REGULAR_RANK_1;
        channel.SamplingTime = board::adcDefaultSamplingTime;
        channel.SingleDiff = ADC_SINGLE_ENDED;
        channel.OffsetNumber = ADC_OFFSET_NONE;
        if (HAL_ADC_ConfigChannel(&handle, &channel) != HAL_OK)
            return HilStatus::failed;

        LL_ADC_REG_SetTriggerSource(handle.Instance, trigger);
        if (HAL_ADC_Start(&handle) != HAL_OK)
            return HilStatus::failed;

        Stopwatch stopwatch;
        HilStatus status = HilStatus::done;
        for (uint32_t sample = 0; sample != samples && status == HilStatus::done; ++sample)
        {
            const auto start = HAL_GetTick();
            while (!LL_ADC_IsActiveFlag_EOC(handle.Instance) && status == HilStatus::done)
                if (HAL_GetTick() - start > sampleTimeoutMs)
                    status = HilStatus::timeout;

            LL_ADC_REG_ReadConversionData12(handle.Instance);
            if (sample == 0)
                stopwatch.Start();
        }

        elapsedUs = stopwatch.ElapsedUs();
        HAL_ADC_Stop(&handle);
        return status;
    }

    std::size_t HrtimCommands::Channels() const
    {
        return opened.config.outputs == Outputs::inPhase && opened.outputPins == OutputPins::both ? 2 : 1;
    }

    void HrtimCommands::ReportTiming()
    {
        auto timing = *pwm->CurrentTiming();
        context.response.Ok() << " clock=" << hal::HighResolutionTimerStm::Clock() << " ratio=" << timing.prescalerRatio << " period=" << timing.period
                              << " dead=" << pwm->DeadTimeTicks() << " dtprsc=" << pwm->DeadTimePrescaler();
    }

    void HrtimCommands::Release()
    {
        pwm.reset();
        timer.reset();
        pins.Release();
        output1Pin = nullptr;
        output2Pin = nullptr;
        faultPin = nullptr;
        openIndex.reset();
        opened = Request();
    }
}

#endif
