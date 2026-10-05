#include "validation/firmware/SyncGpioDriver.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousGpioStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>
#include <optional>

namespace validation::syncgpio
{
    namespace
    {
        constexpr uint8_t ports = static_cast<uint8_t>(hal::Port::I) + 1;
        constexpr uint8_t speeds = static_cast<uint8_t>(hal::Speed::High) + 1;
        constexpr uint8_t pinsPerPort = 16;
        constexpr uint8_t alternateFunctions = 16;

        std::array<std::optional<hal::SynchronousOutputPinStm>, outputSlots> outputs;
        std::array<std::optional<hal::SmallPeripheralPinStm>, peripheralSlots> peripherals;
    }

    bool Open(uint8_t slot, uint8_t port, uint8_t index, bool openDrain, uint8_t speed)
    {
        if (slot >= outputs.size() || outputs[slot] || port >= ports || index >= pinsPerPort || speed >= speeds)
            return false;

        outputs[slot].emplace(static_cast<hal::Port>(port), index, openDrain ? hal::Drive::OpenDrain : hal::Drive::PushPull, static_cast<hal::Speed>(speed));
        return true;
    }

    void Set(uint8_t slot, bool value)
    {
        really_assert(slot < outputs.size() && outputs[slot]);
        outputs[slot]->Set(value);
    }

    bool Latch(uint8_t slot)
    {
        really_assert(slot < outputs.size() && outputs[slot]);
        return outputs[slot]->GetOutputLatch();
    }

    void Close(uint8_t slot)
    {
        really_assert(slot < outputs.size());
        outputs[slot].reset();
    }

    bool OpenAf(uint8_t slot, uint8_t port, uint8_t index, uint8_t af)
    {
        if (slot >= peripherals.size() || peripherals[slot] || port >= ports || index >= pinsPerPort || af >= alternateFunctions)
            return false;

        const hal::SmallPeripheralPinStm::Definition<1> definition{ { { { 0, static_cast<hal::Port>(port), index, af } } }, hal::Drive::Default, hal::Speed::Default, hal::WeakPull::Default };
        peripherals[slot].emplace(definition);
        return true;
    }

    void CloseAf(uint8_t slot)
    {
        really_assert(slot < peripherals.size());
        peripherals[slot].reset();
    }
}
