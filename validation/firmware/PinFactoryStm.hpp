#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/EnumCast.hpp"
#include "services/hil/HilPinPool.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    // One handler per EXTI line serves every port, so a line is owned by at most one pin at a time
    using ExtiOwners = std::array<std::optional<HilPinId>, 16>;

    // Several ADC users may share an analog pin, but the driver reserves a pin once per analog user
    class ManagedPin
        : public hal::GpioPinStm
    {
    public:
        ManagedPin(HilPinId id, hal::Drive drive, hal::Speed speed, hal::WeakPull weakPull, ExtiOwners& extiOwners);

        void ConfigAnalog() override;
        void ResetConfig() override;
        void EnableInterrupt(const infra::Function<void()>& action, hal::InterruptTrigger trigger, hal::InterruptType type = hal::InterruptType::dispatched) override;
        void DisableInterrupt() override;

    private:
        HilPinId id;
        ExtiOwners& extiOwners;
        uint8_t analogUsers = 0;
    };

    constexpr uint16_t Function(hal::PinConfigTypeStm function)
    {
        return static_cast<uint16_t>(infra::enum_cast(function));
    }

    bool IsBonded(HilPinId id);
    bool IsReserved(HilPinId id);
    bool SupportsFunction(HilPinId id, hal::PinConfigTypeStm function, uint8_t peripheralIndex);
    std::optional<HilPinId> FindFunctionPin(hal::PinConfigTypeStm function, uint8_t peripheralIndex);
    bool SupportsAnalog(HilPinId id);
    hal::GpioPinStm& PinOrDummy(hal::GpioPin* pin);

    class PinFactoryStm
        : public services::HilPinFactory
    {
    public:
        static constexpr std::size_t capacity = 32;

        bool IsValid(HilPinId pin) const override;
        bool SupportsFunction(HilPinId pin, uint16_t function, uint8_t instance) const override;
        bool SupportsAnalog(HilPinId pin) const override;
        bool SupportsInterrupt(HilPinId pin) const override;
        std::optional<uint8_t> ParseDrive(infra::BoundedConstString text) const override;

        hal::GpioPin& Construct(std::size_t slot, HilPinId pin, const services::HilPinOptions& options) override;
        void Destroy(std::size_t slot) override;

    private:
        ExtiOwners extiOwners;
        std::array<std::optional<ManagedPin>, capacity> pins;
    };
}
