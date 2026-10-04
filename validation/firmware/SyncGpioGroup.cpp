#include "validation/firmware/SyncGpioGroup.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "generated/stm32fxxx/PinoutTableDefault.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "infra/util/Tokenizer.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"

namespace validation
{
    namespace
    {
        using services::HilArguments;
        using services::HilPinId;
        using services::HilStatus;

        constexpr uint32_t maximumChannel = 4;
        constexpr uint32_t maximumAlternateFunction = 15;

        constexpr std::array<services::HilChoice<uint8_t>, 4> speeds{ {
            { "low", 0 },
            { "medium", 1 },
            { "fast", 2 },
            { "high", 3 },
        } };

        constexpr std::array<hal::PinConfigTypeStm, maximumChannel> channelFunctions{ {
            hal::PinConfigTypeStm::timerChannel1,
            hal::PinConfigTypeStm::timerChannel2,
            hal::PinConfigTypeStm::timerChannel3,
            hal::PinConfigTypeStm::timerChannel4,
        } };

        uint32_t MaximumTimer()
        {
            return static_cast<uint32_t>(hal::peripheralTimer.size());
        }

        uint8_t LetterOffset(HilPinId pin)
        {
            return static_cast<uint8_t>(board::portLetters[pin.port] - 'A');
        }

        std::optional<uint8_t> TimerAlternateFunction(HilPinId pin, uint32_t timer, uint32_t channel)
        {
            if (!IsBonded(pin))
                return std::nullopt;

            for (const auto& subTable : hal::pinoutTableDefaultStm)
                for (const auto& table : subTable)
                    if (table.pinConfigType == channelFunctions[channel - 1])
                        for (const auto& position : table.pinPositions)
                            if (position.peripheralIndex == timer && position.port == PortOf(pin) && position.pin == pin.index)
                                return position.alternateFunction;

            return std::nullopt;
        }

        template<class T, std::size_t N>
        std::optional<uint8_t> FreeSlot(const std::array<std::optional<T>, N>& slots)
        {
            for (uint8_t slot = 0; slot != N; ++slot)
                if (!slots[slot])
                    return slot;

            return std::nullopt;
        }
    }

    SyncGpioCommands::SyncGpioCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , pins(context.pins, owners::syncGpio)
        , commands{ {
              services::HilBind<SyncGpioCommands, &SyncGpioCommands::Out>("sgpio.out", "<pin> <0|1> [od=0|1] [speed=low|medium|fast|high]", *this, context.response),
              services::HilBind<SyncGpioCommands, &SyncGpioCommands::Latch>("sgpio.latch", "<pin>", *this, context.response),
              services::HilBind<SyncGpioCommands, &SyncGpioCommands::Af>("sgpio.af", "<pin> timer=<t> [ch=<1-4>] | af=<0-15>", *this, context.response),
              services::HilBind<SyncGpioCommands, &SyncGpioCommands::Multi>("sgpio.multi", "<pin>,<pin>[,...] timer=<t> [ch=<1-4>]", *this, context.response),
              services::HilBind<SyncGpioCommands, &SyncGpioCommands::Release>("sgpio.release", "<pin>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const SyncGpioCommands::Command> SyncGpioCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus SyncGpioCommands::Out(const HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, { "od", "speed" }))
            return HilStatus::usage;

        uint32_t value = 0;
        bool openDrain = false;
        uint8_t speed = 0;
        HilPinId id{};
        HilStatus status = HilStatus::done;
        arguments.NumberAt(1, value, 0, 1, status);
        arguments.Flag("od", openDrain, status);
        arguments.Select("speed", speed, speeds, status);
        arguments.PinAt(0, context.naming, id, status);
        if (status != HilStatus::done)
            return status;

        auto slot = FindOutput(id);
        if (slot)
        {
            if (!arguments.Has("od"))
                openDrain = outputs[*slot]->openDrain;
            if (!arguments.Has("speed"))
                speed = outputs[*slot]->speed;
        }
        else
        {
            slot = FreeSlot(outputs);
            hal::GpioPin* claimed = nullptr;
            status = pins.Claim(id, services::HilPinPool::Use::exclusive, claimed);
            if (status != HilStatus::done)
                return status;

            if (!slot)
            {
                pins.Release(id);
                return HilStatus::busy;
            }
        }

        auto& output = outputs[*slot];
        if (!output || openDrain != output->openDrain || speed != output->speed)
        {
            if (output)
                syncgpio::Close(*slot);

            const bool opened = syncgpio::Open(*slot, LetterOffset(id), id.index, openDrain, speed);
            really_assert(opened);
            output = Output{ id, openDrain, speed };
        }

        syncgpio::Set(*slot, value != 0);
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus SyncGpioCommands::Latch(const HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        HilPinId id{};
        HilStatus status = HilStatus::done;
        arguments.PinAt(0, context.naming, id, status);
        if (status != HilStatus::done)
            return status;

        const auto slot = FindOutput(id);
        if (!slot)
            return HilStatus::notOpen;

        context.response.Ok() << " value=" << static_cast<uint32_t>(syncgpio::Latch(*slot) ? 1 : 0);
        return HilStatus::done;
    }

    HilStatus SyncGpioCommands::Af(const HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "timer", "ch", "af" }))
            return HilStatus::usage;

        uint32_t timer = 0;
        uint32_t channel = 1;
        uint32_t alternateFunction = 0;
        HilStatus status = HilStatus::done;
        arguments.Number("timer", timer, 1, MaximumTimer(), status);
        arguments.Number("ch", channel, 1, maximumChannel, status);
        arguments.Number("af", alternateFunction, 0, maximumAlternateFunction, status);
        if (status != HilStatus::done)
            return status;

        const bool byTimer = arguments.Has("timer");
        if (byTimer == arguments.Has("af") || (arguments.Has("ch") && !byTimer))
            return HilStatus::usage;

        if (byTimer && !TimerExists(static_cast<uint8_t>(timer)))
            return HilStatus::range;

        HilPinId id{};
        arguments.PinAt(0, context.naming, id, status);
        if (status != HilStatus::done)
            return status;

        if (byTimer)
        {
            const auto found = TimerAlternateFunction(id, timer, channel);
            if (!found)
                return HilStatus::pin;

            alternateFunction = *found;
        }

        const auto slot = FreeSlot(peripherals);
        hal::GpioPin* claimed = nullptr;
        status = pins.Claim(id, services::HilPinPool::Use::exclusive, claimed);
        if (status != HilStatus::done)
            return status;

        if (!slot)
        {
            pins.Release(id);
            return HilStatus::busy;
        }

        const bool opened = syncgpio::OpenAf(*slot, LetterOffset(id), id.index, static_cast<uint8_t>(alternateFunction));
        really_assert(opened);
        peripherals[*slot] = id;
        context.response.Ok() << " af=" << alternateFunction;
        return HilStatus::done;
    }

    HilStatus SyncGpioCommands::Multi(const HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "timer", "ch" }))
            return HilStatus::usage;

        uint32_t timer = 0;
        uint32_t channel = 1;
        HilStatus status = HilStatus::done;
        arguments.Number("timer", timer, 1, MaximumTimer(), status);
        arguments.Number("ch", channel, 1, maximumChannel, status);
        if (status != HilStatus::done)
            return status;

        if (!arguments.Has("timer"))
            return HilStatus::usage;

        if (!TimerExists(static_cast<uint8_t>(timer)))
            return HilStatus::range;

        std::array<HilPinId, maximumMultiPins> ids{};
        std::size_t count = 0;
        status = ParseMultiPins(arguments, ids, count);
        if (status != HilStatus::done)
            return status;

        const auto function = channelFunctions[channel - 1];
        for (std::size_t position = 0; position != count; ++position)
            if (!SupportsFunction(ids[position], function, static_cast<uint8_t>(timer)))
                return HilStatus::pin;

        if (multiPeripheral)
            return HilStatus::busy;

        for (std::size_t position = 0; position != count; ++position)
        {
            hal::GpioPin* claimed = nullptr;
            status = pins.Claim(ids[position], services::HilPinPool::Use::exclusive, claimed);
            if (status != HilStatus::done)
            {
                for (std::size_t held = 0; held != position; ++held)
                    pins.Release(ids[held]);

                return status;
            }

            multiTable[position] = { PortOf(ids[position]), ids[position].index };
        }

        multiIds = ids;
        multiCount = count;
        multiPins.emplace(infra::MemoryRange<const std::pair<hal::Port, uint8_t>>(multiTable.data(), multiTable.data() + count));
        multiPeripheral.emplace(*multiPins, function, static_cast<uint8_t>(timer));
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus SyncGpioCommands::Release(const HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        HilPinId id{};
        HilStatus status = HilStatus::done;
        arguments.PinAt(0, context.naming, id, status);
        if (status != HilStatus::done)
            return status;

        if (const auto slot = FindOutput(id))
        {
            syncgpio::Close(*slot);
            outputs[*slot].reset();
            pins.Release(id);
        }
        else if (const auto peripheral = FindPeripheral(id))
        {
            syncgpio::CloseAf(*peripheral);
            peripherals[*peripheral].reset();
            pins.Release(id);
        }
        else if (InMulti(id))
            ReleaseMulti();
        else
            return HilStatus::notOpen;

        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus SyncGpioCommands::ParseMultiPins(const HilArguments& arguments, std::array<HilPinId, maximumMultiPins>& ids, std::size_t& count) const
    {
        infra::Tokenizer tokens(arguments.Positional(0), ',');
        count = tokens.Size();
        if (count == 0 || count > maximumMultiPins)
            return HilStatus::usage;

        for (std::size_t position = 0; position != count; ++position)
        {
            const auto pin = HilArguments::ParsePin(tokens.Token(position), context.naming);
            if (!pin)
                return HilStatus::pin;

            for (std::size_t earlier = 0; earlier != position; ++earlier)
                if (ids[earlier] == *pin)
                    return HilStatus::usage;

            ids[position] = *pin;
        }

        return HilStatus::done;
    }

    std::optional<uint8_t> SyncGpioCommands::FindOutput(HilPinId pin) const
    {
        for (uint8_t slot = 0; slot != outputs.size(); ++slot)
            if (outputs[slot] && outputs[slot]->pin == pin)
                return slot;

        return std::nullopt;
    }

    std::optional<uint8_t> SyncGpioCommands::FindPeripheral(HilPinId pin) const
    {
        for (uint8_t slot = 0; slot != peripherals.size(); ++slot)
            if (peripherals[slot] == pin)
                return slot;

        return std::nullopt;
    }

    bool SyncGpioCommands::InMulti(HilPinId pin) const
    {
        if (!multiPeripheral)
            return false;

        for (std::size_t position = 0; position != multiCount; ++position)
            if (multiIds[position] == pin)
                return true;

        return false;
    }

    void SyncGpioCommands::ReleaseMulti()
    {
        multiPeripheral.reset();
        multiPins.reset();

        for (std::size_t position = 0; position != multiCount; ++position)
            pins.Release(multiIds[position]);

        multiCount = 0;
    }
}
