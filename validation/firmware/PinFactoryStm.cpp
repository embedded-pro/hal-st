#include "validation/firmware/PinFactoryStm.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PinoutTableDefault.hpp"
#include "services/hil/HilArguments.hpp"
#include <algorithm>

namespace validation
{
    namespace
    {
        constexpr std::array<services::HilChoice<hal::Speed>, 4> speeds{ {
            { "low", hal::Speed::Low },
            { "medium", hal::Speed::Medium },
            { "fast", hal::Speed::Fast },
            { "high", hal::Speed::High },
        } };

        hal::WeakPull ToWeakPull(services::HilPull pull)
        {
            switch (pull)
            {
                case services::HilPull::up:
                    return hal::WeakPull::Up;
                case services::HilPull::down:
                    return hal::WeakPull::Down;
                default:
                    return hal::WeakPull::None;
            }
        }
    }

    ManagedPin::ManagedPin(HilPinId id, hal::Drive drive, hal::Speed speed, hal::WeakPull weakPull, ExtiOwners& extiOwners)
        : hal::GpioPinStm(PortOf(id), id.index, drive, speed, weakPull)
        , id(id)
        , extiOwners(extiOwners)
    {}

    void ManagedPin::ConfigAnalog()
    {
        if (analogUsers++ == 0)
            hal::GpioPinStm::ConfigAnalog();
    }

    void ManagedPin::ResetConfig()
    {
        if (analogUsers > 1)
        {
            --analogUsers;
            return;
        }

        analogUsers = 0;
        hal::GpioPinStm::ResetConfig();
    }

    void ManagedPin::EnableInterrupt(const infra::Function<void()>& action, hal::InterruptTrigger trigger, hal::InterruptType type)
    {
        extiOwners[id.index] = id;
        hal::GpioPinStm::EnableInterrupt(action, trigger, type);
    }

    void ManagedPin::DisableInterrupt()
    {
        if (extiOwners[id.index] != id)
            return;

        hal::GpioPinStm::DisableInterrupt();
        extiOwners[id.index] = std::nullopt;
    }

    bool IsBonded(HilPinId id)
    {
        return id.port < board::bondedPins.size() && id.index <= board::maximumPinIndex && (board::bondedPins[id.port] & (1u << id.index)) != 0;
    }

    bool IsReserved(HilPinId id)
    {
        return std::ranges::find(board::reservedPins, id) != board::reservedPins.end();
    }

    bool SupportsFunction(HilPinId id, hal::PinConfigTypeStm function, uint8_t peripheralIndex)
    {
        if (!IsBonded(id))
            return false;

        for (const auto& subTable : hal::pinoutTableDefaultStm)
            for (const auto& table : subTable)
                if (table.pinConfigType == function)
                    for (const auto& position : table.pinPositions)
                        if (position.peripheralIndex == peripheralIndex && position.port == PortOf(id) && position.pin == id.index)
                            return true;

        return false;
    }

    std::optional<HilPinId> FindFunctionPin(hal::PinConfigTypeStm function, uint8_t peripheralIndex)
    {
        for (const auto& subTable : hal::pinoutTableDefaultStm)
            for (const auto& table : subTable)
                if (table.pinConfigType == function)
                    for (const auto& position : table.pinPositions)
                    {
                        const auto id = Pin(position.port, position.pin);
                        if (position.peripheralIndex == peripheralIndex && IsBonded(id) && !IsReserved(id))
                            return id;
                    }

        return std::nullopt;
    }

    bool SupportsAnalog(HilPinId id)
    {
        if (!IsBonded(id))
            return false;

        for (const auto& position : hal::analogTableDefaultStm)
            if (position.type == hal::Type::adc && position.instance == board::adc && position.port == PortOf(id) && position.pin == id.index)
                return true;

        return false;
    }

    hal::GpioPinStm& PinOrDummy(hal::GpioPin* pin)
    {
        if (pin != nullptr)
            return static_cast<hal::GpioPinStm&>(*pin);

        return hal::dummyPinStm;
    }

    bool PinFactoryStm::IsValid(HilPinId pin) const
    {
        return IsBonded(pin);
    }

    bool PinFactoryStm::SupportsFunction(HilPinId pin, uint16_t function, uint8_t instance) const
    {
        return validation::SupportsFunction(pin, static_cast<hal::PinConfigTypeStm>(function), instance);
    }

    bool PinFactoryStm::SupportsAnalog(HilPinId pin) const
    {
        return validation::SupportsAnalog(pin);
    }

    bool PinFactoryStm::SupportsInterrupt(HilPinId pin) const
    {
        if (!IsBonded(pin) || PortOf(pin) == hal::Port::H)
            return false;

        const auto& owner = extiOwners[pin.index];
        return !owner || *owner == pin;
    }

    std::optional<uint8_t> PinFactoryStm::ParseDrive(infra::BoundedConstString text) const
    {
        if (auto speed = services::HilArguments::ParseChoice(text, speeds))
            return infra::enum_cast(*speed);

        return std::nullopt;
    }

    hal::GpioPin& PinFactoryStm::Construct(std::size_t slot, HilPinId pin, const services::HilPinOptions& options)
    {
        return pins[slot].emplace(pin, options.openDrain ? hal::Drive::OpenDrain : hal::Drive::PushPull, static_cast<hal::Speed>(options.drive), ToWeakPull(options.pull), extiOwners);
    }

    void PinFactoryStm::Destroy(std::size_t slot)
    {
        pins[slot].reset();
    }
}
