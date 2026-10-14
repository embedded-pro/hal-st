#include "validation/firmware/ComparatorFactory.hpp"

#if defined(HAS_PERIPHERAL_COMP)

#include "BoardProfile.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <algorithm>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        struct Blanking
        {
            uint8_t comparator;
            const char* name;
            uint32_t source;
        };

        constexpr uint8_t comparatorInstances = 8;
        constexpr uint32_t maximumDacCode = 4095;
        constexpr uint8_t sharedSources = 0;
        constexpr uint8_t invalidPosition = 0xff;

        constexpr std::array<const char*, 9> openKeys{ { "inp", "inm", "dac", "hyst", "pol", "out", "blank", "sync", "irq" } };

        constexpr std::array<services::HilChoice<bool>, 2> polarities{ {
            { "normal", false },
            { "inverted", true },
        } };

        constexpr std::array<services::HilChoice<hal::ComparatorInterruptsStm::Type>, 2> interruptTypes{ {
            { "dispatched", hal::ComparatorInterruptsStm::Type::dispatched },
            { "immediate", hal::ComparatorInterruptsStm::Type::immediate },
        } };

        constexpr std::array<Blanking, 32> blankings{ {
            { 1, "tim1oc5", COMP_BLANKINGSRC_TIM1_OC5_COMP1 },
            { 1, "tim2oc3", COMP_BLANKINGSRC_TIM2_OC3_COMP1 },
            { 1, "tim3oc3", COMP_BLANKINGSRC_TIM3_OC3_COMP1 },
            { 1, "tim8oc5", COMP_BLANKINGSRC_TIM8_OC5_COMP1 },
            { 2, "tim1oc5", COMP_BLANKINGSRC_TIM1_OC5_COMP2 },
            { 2, "tim2oc3", COMP_BLANKINGSRC_TIM2_OC3_COMP2 },
            { 2, "tim3oc3", COMP_BLANKINGSRC_TIM3_OC3_COMP2 },
            { 2, "tim8oc5", COMP_BLANKINGSRC_TIM8_OC5_COMP2 },
            { 3, "tim1oc5", COMP_BLANKINGSRC_TIM1_OC5_COMP3 },
            { 3, "tim2oc4", COMP_BLANKINGSRC_TIM2_OC4_COMP3 },
            { 3, "tim3oc3", COMP_BLANKINGSRC_TIM3_OC3_COMP3 },
            { 3, "tim8oc5", COMP_BLANKINGSRC_TIM8_OC5_COMP3 },
            { 4, "tim1oc5", COMP_BLANKINGSRC_TIM1_OC5_COMP4 },
            { 4, "tim3oc4", COMP_BLANKINGSRC_TIM3_OC4_COMP4 },
            { 4, "tim8oc5", COMP_BLANKINGSRC_TIM8_OC5_COMP4 },
            { 4, "tim15oc1", COMP_BLANKINGSRC_TIM15_OC1_COMP4 },
            { 5, "tim1oc5", COMP_BLANKINGSRC_TIM1_OC5_COMP5 },
            { 5, "tim2oc3", COMP_BLANKINGSRC_TIM2_OC3_COMP5 },
            { 5, "tim3oc3", COMP_BLANKINGSRC_TIM3_OC3_COMP5 },
            { 5, "tim8oc5", COMP_BLANKINGSRC_TIM8_OC5_COMP5 },
            { 6, "tim1oc5", COMP_BLANKINGSRC_TIM1_OC5_COMP6 },
            { 6, "tim2oc4", COMP_BLANKINGSRC_TIM2_OC4_COMP6 },
            { 6, "tim8oc5", COMP_BLANKINGSRC_TIM8_OC5_COMP6 },
            { 6, "tim15oc2", COMP_BLANKINGSRC_TIM15_OC2_COMP6 },
            { 7, "tim1oc5", COMP_BLANKINGSRC_TIM1_OC5_COMP7 },
            { 7, "tim3oc3", COMP_BLANKINGSRC_TIM3_OC3_COMP7 },
            { 7, "tim8oc5", COMP_BLANKINGSRC_TIM8_OC5_COMP7 },
            { 7, "tim15oc2", COMP_BLANKINGSRC_TIM15_OC2_COMP7 },
            { sharedSources, "tim20oc5", COMP_BLANKINGSRC_TIM20_OC5 },
            { sharedSources, "tim15oc1", COMP_BLANKINGSRC_TIM15_OC1 },
            { sharedSources, "tim4oc3", COMP_BLANKINGSRC_TIM4_OC3 },
            { sharedSources, "none", COMP_BLANKINGSRC_NONE },
        } };

        constexpr std::array<services::HilChoice<uint8_t>, 5> inputMinusChoices{ {
            { "vref/4", 1 },
            { "vref/2", 2 },
            { "vref3/4", 3 },
            { "vref", 4 },
            { "dac", 5 },
        } };

        uint8_t DacResource(DacChannel channel)
        {
            return static_cast<uint8_t>((channel.dac - 1) * 2 + channel.channel - 1);
        }

        std::optional<uint8_t> Position(const std::array<std::optional<HilPinId>, 2>& candidates, HilPinId pin)
        {
            for (uint8_t i = 0; i != candidates.size(); ++i)
                if (candidates[i] == pin)
                    return i;

            return std::nullopt;
        }
    }

    ComparatorFactoryStm::Dac::Dac(uint8_t dacIndex, uint8_t channel)
        : dac(dacIndex)
        , output(channel == 1 ? hal::DigitalToAnalogPinImplStm::Channel::channel1 : hal::DigitalToAnalogPinImplStm::Channel::channel2, dac, hal::DigitalToAnalogPinImplStm::internal)
    {}

    ComparatorFactoryStm::ComparatorFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources)
        : naming(naming)
        , resources(resources)
    {}

    uint8_t ComparatorFactoryStm::Instances() const
    {
        return comparatorInstances;
    }

    infra::MemoryRange<const char* const> ComparatorFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus ComparatorFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus ComparatorFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilComparatorHandle& handle)
    {
        Request request;
        auto status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        const auto& pinsOf = board::comparatorPins[index - 1];
        hal::GpioPin* inputPlus = nullptr;
        hal::GpioPin* inputMinus = nullptr;
        hal::GpioPin* output = nullptr;
        status = pins.Claim(*pinsOf.inputPlus[request.inputPlus], services::HilPinPool::Use::exclusive, inputPlus);
        if (status == HilStatus::done && request.inputMinus == InputMinus::pin)
            status = pins.Claim(*pinsOf.inputMinus[request.inputMinusPin], services::HilPinPool::Use::exclusive, inputMinus);
        if (status == HilStatus::done && request.output)
            status = pins.ClaimFunction(*request.output, Function(hal::PinConfigTypeStm::compOutput), index, output);
        if (status == HilStatus::done && request.inputMinus == InputMinus::dac)
        {
            status = resources.Claim(Resource::dac, DacResource(pinsOf.internalDac), services::HilOwners::comparator);
            if (status == HilStatus::done)
                dacResource = DacResource(pinsOf.internalDac);
        }

        if (status != HilStatus::done)
        {
            Release();
            return status;
        }

        if (request.inputMinus == InputMinus::dac)
            dac.emplace(pinsOf.internalDac.dac, pinsOf.internalDac.channel).output.Set(request.dacCode);

        openIndex = index;
        auto config = Config(request);
        if (request.synchronous)
            handle.synchronous = &driver.emplace<hal::SynchronousComparatorStm>(index, PinOrDummy(inputPlus), PinOrDummy(inputMinus), PinOrDummy(output), config);
        else
            handle.comparator = &driver.emplace<hal::ComparatorStm>(index, PinOrDummy(inputPlus), PinOrDummy(inputMinus), PinOrDummy(output), config);

        return HilStatus::done;
    }

    void ComparatorFactoryStm::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        Release();
        onClosed();
    }

    HilStatus ComparatorFactoryStm::SetDac(uint8_t index, uint16_t code)
    {
        if (!dac || openIndex != index)
            return HilStatus::notOpen;

        dac->output.Set(code);
        return HilStatus::done;
    }

    HilStatus ComparatorFactoryStm::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (index == 0 || index > hal::peripheralComp.size())
            return HilStatus::range;

        std::optional<HilPinId> inputPlus;
        uint32_t dacCode = request.dacCode;
        uint32_t hysteresis = 0;
        HilStatus status = HilStatus::done;
        arguments.Pin("inp", naming, inputPlus, status);
        if (status == HilStatus::done)
            status = ParseInputMinus(index, arguments, request);
        arguments.Number("dac", dacCode, 0, maximumDacCode, status);
        arguments.Number("hyst", hysteresis, 0, hal::ComparatorBaseStm::maximumHysteresisMillivolts, status);
        arguments.Select("pol", request.inverted, polarities, status);
        arguments.Pin("out", naming, request.output, status);
        if (status == HilStatus::done)
            status = ParseBlanking(index, arguments, request);
        arguments.Flag("sync", request.synchronous, status);
        arguments.Select("irq", request.interruptType, interruptTypes, status);
        if (status != HilStatus::done)
            return status;

        if (arguments.Has("dac") && request.inputMinus != InputMinus::dac)
            return HilStatus::usage;

        if (hysteresis % 10 != 0)
            return HilStatus::range;

        request.dacCode = static_cast<uint16_t>(dacCode);
        request.hysteresis = static_cast<uint8_t>(hysteresis);

        const auto& pinsOf = board::comparatorPins[index - 1];
        if (inputPlus)
        {
            auto position = Position(pinsOf.inputPlus, *inputPlus);
            if (!position)
                return HilStatus::pin;

            request.inputPlus = *position;
        }

        if (request.inputMinus == InputMinus::pin && request.inputMinusPin == invalidPosition)
            return HilStatus::pin;

        if (request.output && !SupportsFunction(*request.output, hal::PinConfigTypeStm::compOutput, index))
            return HilStatus::pin;

        if (!request.blankingAvailable)
            return HilStatus::unsupported;

        return HilStatus::done;
    }

    HilStatus ComparatorFactoryStm::ParseInputMinus(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        auto text = arguments.Key("inm");
        if (!text)
            return HilStatus::done;

        if (auto choice = services::HilArguments::ParseChoice(*text, inputMinusChoices))
        {
            request.inputMinus = static_cast<InputMinus>(*choice);
            return HilStatus::done;
        }

        auto pin = services::HilArguments::ParsePin(*text, naming);
        if (!pin)
            return HilStatus::pin;

        request.inputMinus = InputMinus::pin;
        request.inputMinusPin = Position(board::comparatorPins[index - 1].inputMinus, *pin).value_or(invalidPosition);
        return HilStatus::done;
    }

    HilStatus ComparatorFactoryStm::ParseBlanking(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        auto text = arguments.Key("blank");
        if (!text)
            return HilStatus::done;

        auto named = std::ranges::any_of(blankings, [&text](const Blanking& blanking)
            {
                return *text == blanking.name;
            });
        if (!named)
            return HilStatus::usage;

        auto available = std::ranges::find_if(blankings, [&text, index](const Blanking& blanking)
            {
                return *text == blanking.name && (blanking.comparator == index || blanking.comparator == sharedSources);
            });
        request.blankingAvailable = available != blankings.end();
        if (request.blankingAvailable)
            request.blanking = available->source;

        return HilStatus::done;
    }

    hal::ComparatorBaseStm::Config ComparatorFactoryStm::Config(const Request& request) const
    {
        using InputMinusConfig = hal::ComparatorBaseStm::Config::InputMinus;
        constexpr std::array<InputMinusConfig, 6> inputMinusConfigs{ {
            InputMinusConfig::io1,
            InputMinusConfig::vrefintQuarter,
            InputMinusConfig::vrefintHalf,
            InputMinusConfig::vrefintThreeQuarters,
            InputMinusConfig::vrefint,
            InputMinusConfig::dacInternal,
        } };

        hal::ComparatorBaseStm::Config config;
        config.inputPlus = request.inputPlus == 0 ? hal::ComparatorBaseStm::Config::InputPlus::io1 : hal::ComparatorBaseStm::Config::InputPlus::io2;
        config.inputMinus = inputMinusConfigs[static_cast<std::size_t>(request.inputMinus)];
        if (request.inputMinus == InputMinus::pin && request.inputMinusPin == 1)
            config.inputMinus = InputMinusConfig::io2;
        config.hysteresisMillivolts = request.hysteresis;
        config.invertOutput = request.inverted;
        config.blanking = request.blanking;
        config.interruptType = request.interruptType;
        return config;
    }

    void ComparatorFactoryStm::Release()
    {
        openIndex.reset();
        driver.emplace<std::monostate>();
        dac.reset();

        if (dacResource)
            resources.Release(Resource::dac, *dacResource, services::HilOwners::comparator);

        dacResource.reset();
    }

    ComparatorExtensionCommands::ComparatorExtensionCommands(services::HilContext& context, ComparatorFactoryStm& factory)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , factory(factory)
        , commands{ {
              services::HilBind<ComparatorExtensionCommands, &ComparatorExtensionCommands::Dac>("comp.dac", "<index> <code>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const ComparatorExtensionCommands::Command> ComparatorExtensionCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus ComparatorExtensionCommands::Dac(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, {}))
            return HilStatus::usage;

        uint32_t index = 0;
        uint32_t code = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 0, comparatorInstances - 1, status);
        arguments.NumberAt(1, code, 0, maximumDacCode, status);
        if (status != HilStatus::done)
            return status;

        status = factory.SetDac(static_cast<uint8_t>(index), static_cast<uint16_t>(code));
        if (status == HilStatus::done)
            context.response.Ok() << " value=" << code;

        return status;
    }
}

#endif
