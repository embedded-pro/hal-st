#include "validation/firmware/QeiFactory.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <chrono>
#include <limits>
#include <type_traits>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using Config = hal::SynchronousQuadratureEncoderStm::Config;
        using services::HilChoice;
        using services::HilStatus;

        constexpr uint32_t defaultVelocityPeriodUs = 1000;
        constexpr uint32_t maximumVelocityPeriodUs = 1000000;
        constexpr uint32_t maximumFilter = 15;
        constexpr uint32_t maximumResolution16Bit = 65536;

        constexpr std::array<const char*, 11> openKeys{ { "lp", "a", "b", "idx", "res", "offset", "inva", "invb", "cap", "filter", "vel" } };

        constexpr std::array<HilChoice<Config::DecodeMode>, 3> decodeModes{ {
            { "a", Config::DecodeMode::x2OnPhaseA },
            { "b", Config::DecodeMode::x2OnPhaseB },
            { "ab", Config::DecodeMode::x4OnBothPhases },
        } };

        constexpr std::array<hal::PinConfigTypeStm, 2> timerInputs{ { hal::PinConfigTypeStm::timerChannel1, hal::PinConfigTypeStm::timerChannel2 } };
        constexpr std::array<hal::PinConfigTypeStm, 2> lowPowerTimerInputs{ { hal::PinConfigTypeStm::lpTimerInput1, hal::PinConfigTypeStm::lpTimerInput2 } };

        HilStatus CheckTimer(uint8_t timer)
        {
            if (!TimerExists(timer))
                return HilStatus::range;

            if (!IS_TIM_ENCODER_INTERFACE_INSTANCE(hal::peripheralTimer[timer - 1]))
                return HilStatus::unsupported;

            return HilStatus::done;
        }

        // PROTOCOL numbers only the LPTIMs with an encoder interface, so another LPTIM is out of range rather than unsupported
        HilStatus CheckLowPowerTimer([[maybe_unused]] uint8_t timer)
        {
#if defined(HAS_PERIPHERAL_LPTIMER)
            if (timer < 1 || timer > hal::peripheralLpTimer.size() || hal::peripheralLpTimer[timer - 1] == nullptr || !IS_LPTIM_ENCODER_INTERFACE_INSTANCE(hal::peripheralLpTimer[timer - 1]))
                return HilStatus::range;

            return HilStatus::done;
#else
            return HilStatus::unsupported;
#endif
        }

        uint32_t MaximumResolution(uint8_t timer, bool lowPower)
        {
            if (!lowPower && IS_TIM_32B_COUNTER_INSTANCE(hal::peripheralTimer[timer - 1]))
                return std::numeric_limits<uint32_t>::max();

            return maximumResolution16Bit;
        }

        bool IsLowPowerFilter(uint8_t samples)
        {
            return samples == 0 || samples == 2 || samples == 4 || samples == 8;
        }

#if defined(HAS_PERIPHERAL_LPTIMER)
        hal::SynchronousQuadratureEncoderLpTimStm::Config::Filter LowPowerFilter(uint8_t samples)
        {
            using Filter = hal::SynchronousQuadratureEncoderLpTimStm::Config::Filter;

            switch (samples)
            {
                case 2:
                    return Filter::twoSamples;
                case 4:
                    return Filter::fourSamples;
                case 8:
                    return Filter::eightSamples;
                default:
                    return Filter::none;
            }
        }

        hal::SynchronousQuadratureEncoderLpTimStm::Config LowPowerConfig(const Config& config)
        {
            hal::SynchronousQuadratureEncoderLpTimStm::Config result;
            result.resolution = config.resolution;
            result.filter = LowPowerFilter(config.filter);
            result.reverseForMirroredMounting = config.invertPhaseA;
            result.speedSamplePeriod = config.speedSamplePeriod;
            return result;
        }
#endif

        template<class Encoder>
        hal::SynchronousQuadratureEncoder& WithIndex(Encoder& encoder, const hal::GpioPin* index)
        {
            if (index != nullptr)
                encoder.EnableIndex();

            return encoder;
        }
    }

    QeiFactoryStm::QeiFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers)
        : naming(naming)
        , timers(timers)
    {}

    uint8_t QeiFactoryStm::Instances() const
    {
        return 18;
    }

    infra::MemoryRange<const char* const> QeiFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus QeiFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus QeiFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, hal::SynchronousQuadratureEncoder*& encoder)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        if (!request.lowPower)
        {
            status = timers.Claim(index, services::HilOwners::qei);
            if (status != HilStatus::done)
                return status;
        }

        ClaimedPins claimed;
        status = Claim(index, request, pins, claimed);
        if (status != HilStatus::done)
        {
            ReleaseTimer(index, request.lowPower);
            return status;
        }

        encoder = &Construct(index, request, claimed);
        opened = OpenedInstance{ index, request.lowPower, request.index.has_value() };
        return HilStatus::done;
    }

    void QeiFactoryStm::Close(uint8_t index, const infra::Function<void()>& onClosed)
    {
        driver.emplace<std::monostate>();

        if (opened)
            ReleaseTimer(index, opened->lowPower);

        opened.reset();
        onClosed();
    }

    HilStatus QeiFactoryStm::ReadIndex(uint8_t index, bool& asserted) const
    {
        if (!opened || opened->timer != index)
            return HilStatus::notOpen;

        if (!opened->hasIndex)
            return HilStatus::unsupported;

        asserted = std::visit([]<class Alternative>(const Alternative& alternative)
            {
                if constexpr (std::is_same_v<Alternative, std::monostate>)
                    return false;
                else
                    return alternative.IndexAsserted();
            },
            driver);

        return HilStatus::done;
    }

    HilStatus QeiFactoryStm::Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const
    {
        HilStatus status = HilStatus::done;
        arguments.Flag("lp", request.lowPower, status);
        if (status != HilStatus::done)
            return status;

        status = request.lowPower ? CheckLowPowerTimer(timer) : CheckTimer(timer);
        if (status != HilStatus::done)
            return status;

        status = ParseSettings(timer, arguments, request);
        if (status != HilStatus::done)
            return status;

        return ParsePins(timer, arguments, request);
    }

    HilStatus QeiFactoryStm::ParseSettings(uint8_t timer, const services::HilArguments& arguments, Request& request) const
    {
        auto& config = request.config;

        HilStatus status = HilStatus::done;
        arguments.Number("res", config.resolution, 2, MaximumResolution(timer, request.lowPower), status);
        arguments.Number("offset", config.offset, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status == HilStatus::done && config.offset >= config.resolution)
            return HilStatus::range;

        uint32_t filter = 0;
        arguments.Flag("inva", config.invertPhaseA, status);
        arguments.Flag("invb", config.invertPhaseB, status);
        arguments.Select("cap", config.decodeMode, decodeModes, status);
        arguments.Number("filter", filter, 0, maximumFilter, status);
        config.filter = static_cast<uint8_t>(filter);

        if (auto velocity = arguments.Key("vel"); velocity && *velocity == "off")
            config.speedSamplePeriod = std::nullopt;
        else
        {
            uint32_t velocityPeriod = defaultVelocityPeriodUs;
            arguments.Number("vel", velocityPeriod, 1, maximumVelocityPeriodUs, status);
            config.speedSamplePeriod = std::chrono::microseconds(velocityPeriod);
        }

        if (status != HilStatus::done)
            return status;

        if (request.lowPower && (arguments.Has("cap") || arguments.Has("offset") || arguments.Has("invb")))
            return HilStatus::unsupported;

        if (request.lowPower && !IsLowPowerFilter(config.filter))
            return HilStatus::range;

        return HilStatus::done;
    }

    HilStatus QeiFactoryStm::ParsePins(uint8_t timer, const services::HilArguments& arguments, Request& request) const
    {
        HilStatus status = HilStatus::done;
        arguments.Pin("a", naming, request.a, status);
        arguments.Pin("b", naming, request.b, status);
        arguments.Pin("idx", naming, request.index, status);
        if (status != HilStatus::done)
            return status;

        if (!request.a && !request.b && !request.index && !request.lowPower && timer == board::defaultQei.timer)
        {
            request.a = board::defaultQei.a;
            request.b = board::defaultQei.b;
            request.index = board::defaultQei.idx;
        }

        if (!request.a || !request.b)
            return HilStatus::usage;

        const auto& inputs = request.lowPower ? lowPowerTimerInputs : timerInputs;
        if (!SupportsFunction(*request.a, inputs[0], timer) || !SupportsFunction(*request.b, inputs[1], timer))
            return HilStatus::pin;

        if (request.index && !IsBonded(*request.index))
            return HilStatus::pin;

        return HilStatus::done;
    }

    HilStatus QeiFactoryStm::Claim(uint8_t timer, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const
    {
        const auto& inputs = request.lowPower ? lowPowerTimerInputs : timerInputs;

        HilStatus status = pins.ClaimFunction(*request.a, Function(inputs[0]), timer, claimed.a);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(*request.b, Function(inputs[1]), timer, claimed.b);
        if (status == HilStatus::done && request.index)
            status = pins.Claim(*request.index, services::HilPinPool::Use::exclusive, claimed.index);

        return status;
    }

    hal::SynchronousQuadratureEncoder& QeiFactoryStm::Construct(uint8_t timer, const Request& request, const ClaimedPins& claimed)
    {
#if defined(HAS_PERIPHERAL_LPTIMER)
        if (request.lowPower)
            return WithIndex(driver.emplace<hal::SynchronousQuadratureEncoderLpTimStm>(timer, PinOrDummy(claimed.a), PinOrDummy(claimed.b), PinOrDummy(claimed.index), LowPowerConfig(request.config)), claimed.index);
#endif

        return WithIndex(driver.emplace<hal::SynchronousQuadratureEncoderStm>(timer, PinOrDummy(claimed.a), PinOrDummy(claimed.b), PinOrDummy(claimed.index), request.config), claimed.index);
    }

    void QeiFactoryStm::ReleaseTimer(uint8_t timer, bool lowPower)
    {
        if (!lowPower)
            timers.Release(timer, services::HilOwners::qei);
    }

    QeiExtensionCommands::QeiExtensionCommands(services::HilContext& context, QeiFactoryStm& factory)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , factory(factory)
        , commands{ {
              services::HilBind<QeiExtensionCommands, &QeiExtensionCommands::Index>("qei.index", "qei.index <timer>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const QeiExtensionCommands::Command> QeiExtensionCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus QeiExtensionCommands::Index(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        uint32_t index = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 0, factory.Instances() - 1, status);
        if (status != HilStatus::done)
            return status;

        bool asserted = false;
        status = factory.ReadIndex(static_cast<uint8_t>(index), asserted);
        if (status != HilStatus::done)
            return status;

        context.response.Ok() << " idx=" << static_cast<uint32_t>(asserted ? 1 : 0);
        return HilStatus::done;
    }
}
