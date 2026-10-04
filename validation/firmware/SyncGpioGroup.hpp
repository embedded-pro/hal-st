#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinPool.hpp"
#include "validation/firmware/SyncGpioDriver.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace validation
{
    class SyncGpioCommands
        : public services::TerminalCommands
    {
    public:
        static constexpr std::size_t maximumMultiPins = 4;

        explicit SyncGpioCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Output
        {
            services::HilPinId pin;
            bool openDrain;
            uint8_t speed;
        };

        services::HilStatus Out(const services::HilArguments& arguments);
        services::HilStatus Latch(const services::HilArguments& arguments);
        services::HilStatus Af(const services::HilArguments& arguments);
        services::HilStatus Multi(const services::HilArguments& arguments);
        services::HilStatus Release(const services::HilArguments& arguments);

        services::HilStatus ParseMultiPins(const services::HilArguments& arguments, std::array<services::HilPinId, maximumMultiPins>& ids, std::size_t& count) const;
        std::optional<uint8_t> FindOutput(services::HilPinId pin) const;
        std::optional<uint8_t> FindPeripheral(services::HilPinId pin) const;
        bool InMulti(services::HilPinId pin) const;
        void ReleaseMulti();

    private:
        services::HilContext& context;
        services::HilPinOwner pins;
        std::array<std::optional<Output>, syncgpio::outputSlots> outputs;
        std::array<std::optional<services::HilPinId>, syncgpio::peripheralSlots> peripherals;
        std::array<services::HilPinId, maximumMultiPins> multiIds{};
        std::array<std::pair<hal::Port, uint8_t>, maximumMultiPins> multiTable{};
        std::size_t multiCount = 0;
        std::optional<hal::MultiGpioPinStm> multiPins;
        std::optional<hal::MultiPeripheralPinStm> multiPeripheral;
        std::array<Command, 5> commands;
    };
}
