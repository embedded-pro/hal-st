#include "validation/firmware/DacGroup.hpp"

#if defined(HAS_PERIPHERAL_DAC)

#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <algorithm>

namespace validation
{
    namespace
    {
        using services::HilPinId;
        using services::HilStatus;

        constexpr uint32_t maximumValue = 0xffff;
        constexpr uint32_t resolutionMask = 0xfff;

        std::optional<std::size_t> OutputOfPin(HilPinId pin)
        {
            for (std::size_t index = 0; index != board::dacOutputs.size(); ++index)
                if (board::dacOutputs[index].pin == pin)
                    return index;

            return std::nullopt;
        }
    }

    DacCommands::Output::Output(HilPinId id, uint8_t dacIndex, hal::GpioPinStm& pin)
        : id(id)
        , dac(dacIndex)
        , output(pin, dac, hal::DigitalToAnalogPinImplStm::external)
    {}

    DacCommands::DacCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , pins(context.pins, owners::dac)
        , commands{ {
              services::HilBind<DacCommands, &DacCommands::Open>("dac.open", "<pin>", *this, context.response),
              services::HilBind<DacCommands, &DacCommands::Set>("dac.set", "<pin> <value>", *this, context.response),
              services::HilBind<DacCommands, &DacCommands::Close>("dac.close", "<pin>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const DacCommands::Command> DacCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus DacCommands::ParseOutput(const services::HilArguments& arguments, HilPinId& id, std::optional<std::size_t>& slot) const
    {
        HilStatus status = HilStatus::done;
        arguments.PinAt(0, context.naming, id, status);
        if (status != HilStatus::done)
            return status;

        slot = OutputOfPin(id);
        return slot ? HilStatus::done : HilStatus::pin;
    }

    HilStatus DacCommands::Open(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        HilPinId id{};
        std::optional<std::size_t> slot;
        auto status = ParseOutput(arguments, id, slot);
        if (status != HilStatus::done)
            return status;

        if (outputs[*slot])
            return HilStatus::busy;

        hal::GpioPin* claimed = nullptr;
        status = pins.Claim(id, services::HilPinPool::Use::exclusive, claimed);
        if (status != HilStatus::done)
            return status;

        const auto dacIndex = board::dacOutputs[*slot].dac;
        outputs[*slot].emplace(id, dacIndex, PinOrDummy(claimed));
        context.response.Ok() << " dac=" << static_cast<uint32_t>(dacIndex);
        return HilStatus::done;
    }

    HilStatus DacCommands::Set(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, {}))
            return HilStatus::usage;

        uint32_t value = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(1, value, 0, maximumValue, status);
        if (status != HilStatus::done)
            return status;

        HilPinId id{};
        std::optional<std::size_t> slot;
        status = ParseOutput(arguments, id, slot);
        if (status != HilStatus::done)
            return status;

        if (!outputs[*slot])
            return HilStatus::notOpen;

        outputs[*slot]->output.Set(static_cast<uint16_t>(value));
        context.response.Ok() << " value=" << std::min(value, resolutionMask);
        return HilStatus::done;
    }

    HilStatus DacCommands::Close(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        HilPinId id{};
        std::optional<std::size_t> slot;
        const auto status = ParseOutput(arguments, id, slot);
        if (status != HilStatus::done)
            return status;

        if (!outputs[*slot])
            return HilStatus::notOpen;

        outputs[*slot].reset();
        pins.Release(id);
        context.response.Ok();
        return HilStatus::done;
    }
}

#endif
