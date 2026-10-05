#pragma once

#include "hal_st/stm32fxxx/WatchDogStm.hpp"
#include "services/hil/HilPinPool.hpp"
#include "services/hil/commands/HilWatchDogCommands.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include <cstdint>
#include <optional>

namespace validation
{
    class WatchDogFactoryStm
        : public services::HilWatchDogFactory
    {
    public:
        WatchDogFactoryStm(const services::HilPinNaming& naming, services::HilPinPool& pins);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> StartKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        services::HilStatus Create(uint8_t index, infra::Duration timeout, const services::HilArguments& arguments, hal::Watchdog*& watchDog) override;

        hal::WatchDogStm& Borrow();
        void Return();

    private:
        class WarningToggle
            : public hal::Watchdog
        {
        public:
            WarningToggle(hal::Watchdog& watchDog, hal::GpioPin& pin);

            infra::Duration EarlyWarningPeriod() const override;
            void Start(const infra::Function<void()>& onEarlyWarning) override;
            void Refresh() override;

        private:
            hal::Watchdog& watchDog;
            hal::GpioPin& pin;
            infra::Function<void()> onEarlyWarning;
        };

        struct Request
        {
            uint32_t timeoutMs = 0;
            std::optional<HilPinId> pin;
        };

        services::HilStatus Parse(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Validate(const Request& request, uint32_t& prescaler) const;

    private:
        const services::HilPinNaming& naming;
        services::HilPinOwner warningPins;
        std::optional<hal::WatchDogStm> watchDog;
        std::optional<WarningToggle> toggle;
        bool started = false;
    };
}
