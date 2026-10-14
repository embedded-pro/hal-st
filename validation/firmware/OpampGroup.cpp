#include "validation/firmware/OpampGroup.hpp"

#if defined(HAS_PERIPHERAL_OPAMP)

#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <chrono>

namespace validation
{
    namespace
    {
        using services::HilStatus;
        using Config = hal::OpampStm::Config;

        constexpr std::chrono::milliseconds readTimeout{ 1000 };
        constexpr uint32_t maximumDacCode = 4095;
        constexpr uint32_t maximumGain = 64;

        constexpr std::array<const char*, 9> openKeys{ { "mode", "inp", "inm", "pga", "gain", "out", "speed", "cal", "dac" } };

        constexpr std::array<services::HilChoice<Config::Mode>, 3> modes{ {
            { "follower", Config::Mode::follower },
            { "pga", Config::Mode::pga },
            { "standalone", Config::Mode::standalone },
        } };

        constexpr std::array<services::HilChoice<Config::PgaConnection>, 3> pgaConnections{ {
            { "none", Config::PgaConnection::none },
            { "filter", Config::PgaConnection::vinm0Filter },
            { "bias", Config::PgaConnection::vinm0Bias },
        } };

        constexpr std::array<services::HilChoice<bool>, 2> outputs{ {
            { "internal", false },
            { "pin", true },
        } };

        constexpr std::array<services::HilChoice<bool>, 2> speeds{ {
            { "normal", false },
            { "high", true },
        } };

        template<std::size_t N>
        std::optional<uint8_t> Position(const std::array<std::optional<HilPinId>, N>& candidates, HilPinId pin)
        {
            for (uint8_t i = 0; i != N; ++i)
                if (candidates[i] == pin)
                    return i;

            return std::nullopt;
        }

        uint8_t DacResource(DacChannel channel)
        {
            return static_cast<uint8_t>((channel.dac - 1) * 2 + channel.channel - 1);
        }
    }

    OpampCommands::Dac::Dac(uint8_t dacIndex, uint8_t channel)
        : dac(dacIndex)
        , output(channel == 1 ? hal::DigitalToAnalogPinImplStm::Channel::channel1 : hal::DigitalToAnalogPinImplStm::Channel::channel2, dac, hal::DigitalToAnalogPinImplStm::internal)
    {}

    OpampCommands::OpampCommands(services::HilContext& context, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , resources(resources)
        , pins(context.pins, owners::opamp)
        , pending(context.response)
        , commands{ {
              services::HilBind<OpampCommands, &OpampCommands::Open>("opamp.open", "<index> [key=value]...", *this, context.response),
              services::HilBind<OpampCommands, &OpampCommands::SetDac>("opamp.dac", "<index> <code>", *this, context.response),
              services::HilBind<OpampCommands, &OpampCommands::Read>("opamp.read", "<index> [sampling=]", *this, context.response),
              services::HilBind<OpampCommands, &OpampCommands::Close>("opamp.close", "<index>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const OpampCommands::Command> OpampCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus OpampCommands::Open(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(openKeys)))
            return HilStatus::usage;

        uint32_t index = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 1, board::opampPins.size(), status);
        if (status != HilStatus::done)
            return status;

        Request request;
        status = Parse(arguments, static_cast<uint8_t>(index), request);
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

        const auto& pinsOf = board::opampPins[index - 1];
        if (request.config.input == Config::Input::dac)
            dac.emplace(pinsOf.dac->dac, pinsOf.dac->channel).output.Set(request.dacCode);

        auto& amplifier = opamp.emplace(static_cast<uint8_t>(index), PinOrDummy(inputPlusPin), PinOrDummy(inputMinusPin), PinOrDummy(outputPin), request.config);
        openIndex = static_cast<uint8_t>(index);
        internalOutput = request.config.internalOutput;
        context.response.Ok() << " trimp=" << amplifier.TrimmingP() << " trimn=" << amplifier.TrimmingN();
        return HilStatus::done;
    }

    HilStatus OpampCommands::SetDac(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, {}))
            return HilStatus::usage;

        uint32_t code = 0;
        HilStatus status = Find(arguments);
        arguments.NumberAt(1, code, 0, maximumDacCode, status);
        if (status != HilStatus::done)
            return status;

        if (!dac)
            return HilStatus::notOpen;

        dac->output.Set(static_cast<uint16_t>(code));
        context.response.Ok() << " value=" << code;
        return HilStatus::done;
    }

    HilStatus OpampCommands::Read(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "sampling" }))
            return HilStatus::usage;

        uint32_t samplingTime = ADC_SAMPLETIME_640CYCLES_5;
        HilStatus status = Find(arguments);
        arguments.Select("sampling", samplingTime, board::adcSamplingTimes, status);
        if (status != HilStatus::done)
            return status;

        const auto& pinsOf = board::opampPins[*openIndex - 1];
        if (internalOutput ? !pinsOf.internalAdcChannel : !SupportsAnalog(pinsOf.output))
            return HilStatus::unsupported;

        if (pending.Busy())
            return HilStatus::busy;

        status = resources.Claim(Resource::adc, board::adc, owners::opamp);
        if (status != HilStatus::done)
            return status;

        adcClaimed = true;
        hal::detail::AdcStmChannelConfig config;
        config.samplingTime = samplingTime;
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

        if (internalOutput)
            reader.emplace<hal::AnalogToDigitalChannelStm>(converter, *pinsOf.internalAdcChannel, config).Measure(1, onDone);
        else
            reader.emplace<hal::AnalogToDigitalPinImplStm>(PinOrDummy(outputPin), converter, config).Measure(1, onDone);

        return HilStatus::done;
    }

    HilStatus OpampCommands::Close(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        auto status = Find(arguments);
        if (status != HilStatus::done)
            return status;

        if (pending.Busy())
            return HilStatus::busy;

        Release();
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus OpampCommands::Parse(const services::HilArguments& arguments, uint8_t index, Request& request) const
    {
        auto& config = request.config;
        uint32_t gain = config.gain;
        uint32_t dacCode = 0;
        HilStatus status = HilStatus::done;
        arguments.Select("mode", config.mode, modes, status);
        if (auto input = arguments.Key("inp"); input && status == HilStatus::done)
        {
            if (*input == "dac")
                config.input = Config::Input::dac;
            else
            {
                request.inputPlus = services::HilArguments::ParsePin(*input, context.naming);
                if (!request.inputPlus)
                    status = HilStatus::pin;
            }
        }
        arguments.Pin("inm", context.naming, request.inputMinus, status);
        arguments.Select("pga", config.pgaConnection, pgaConnections, status);
        arguments.Number("gain", gain, 0, maximumGain, status);
        arguments.Select("out", request.pinOutput, outputs, status);
        arguments.Select("speed", config.highSpeed, speeds, status);
        arguments.Flag("cal", config.calibrate, status);
        arguments.Number("dac", dacCode, 0, maximumDacCode, status);
        if (status != HilStatus::done)
            return status;

        const bool pga = config.mode == Config::Mode::pga;
        if ((arguments.Has("gain") || arguments.Has("pga")) && !pga)
            return HilStatus::usage;

        if (arguments.Has("dac") && config.input != Config::Input::dac)
            return HilStatus::usage;

        config.gain = static_cast<uint8_t>(gain);
        config.internalOutput = !request.pinOutput;
        request.dacCode = static_cast<uint16_t>(dacCode);
        if (hal::OpampStm::UsesInvertingPin(config) != request.inputMinus.has_value())
            return HilStatus::usage;

        if (pga && !hal::OpampStm::ValidGain(config.gain))
            return HilStatus::range;

        return Check(index, request);
    }

    HilStatus OpampCommands::Check(uint8_t index, Request& request) const
    {
        const auto& pinsOf = board::opampPins[index - 1];
        auto& config = request.config;

        if (request.inputPlus)
        {
            auto position = Position(pinsOf.inputPlus, *request.inputPlus);
            if (!position)
                return HilStatus::pin;

            config.input = static_cast<Config::Input>(*position);
        }
        else if (config.input != Config::Input::dac)
            request.inputPlus = pinsOf.inputPlus[0];

        if (request.inputMinus)
        {
            auto position = Position(pinsOf.inputMinus, *request.inputMinus);
            if (!position || (config.mode == Config::Mode::pga && *position != 0))
                return HilStatus::pin;

            config.invertingInput = *position == 0 ? Config::InvertingInput::vinm0 : Config::InvertingInput::vinm1;
        }

        if (config.input == Config::Input::dac && !pinsOf.dac)
            return HilStatus::unsupported;

        return HilStatus::done;
    }

    HilStatus OpampCommands::Claim(uint8_t index, const Request& request)
    {
        const auto& pinsOf = board::opampPins[index - 1];
        HilStatus status = HilStatus::done;
        if (request.inputPlus)
            status = pins.Claim(*request.inputPlus, services::HilPinPool::Use::exclusive, inputPlusPin);
        if (status == HilStatus::done && request.inputMinus)
            status = pins.Claim(*request.inputMinus, services::HilPinPool::Use::exclusive, inputMinusPin);
        if (status == HilStatus::done && request.pinOutput)
            status = pins.Claim(pinsOf.output, services::HilPinPool::Use::analog, outputPin);
        if (status == HilStatus::done && request.config.input == Config::Input::dac)
        {
            status = resources.Claim(Resource::dac, DacResource(*pinsOf.dac), owners::opamp);
            if (status == HilStatus::done)
                dacResource = DacResource(*pinsOf.dac);
        }

        return status;
    }

    HilStatus OpampCommands::Find(const services::HilArguments& arguments) const
    {
        uint32_t index = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 1, board::opampPins.size(), status);
        if (status == HilStatus::done && openIndex != index)
            return HilStatus::notOpen;

        return status;
    }

    void OpampCommands::FinishRead()
    {
        reader.emplace<std::monostate>();
        adc.reset();
        if (adcClaimed)
            resources.Release(Resource::adc, board::adc, owners::opamp);
        adcClaimed = false;

        if (pending.Complete(operation))
            context.response.Ok() << " code=" << static_cast<uint32_t>(code);
    }

    void OpampCommands::Release()
    {
        opamp.reset();
        dac.reset();
        if (dacResource)
            resources.Release(Resource::dac, *dacResource, owners::opamp);

        dacResource.reset();
        pins.Release();
        inputPlusPin = nullptr;
        inputMinusPin = nullptr;
        outputPin = nullptr;
        openIndex.reset();
    }
}

#endif
