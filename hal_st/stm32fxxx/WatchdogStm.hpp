#ifndef HAL_WATCHDOG_STM_HPP
#define HAL_WATCHDOG_STM_HPP

#include DEVICE_HEADER
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/Watchdog.hpp"
#include "infra/util/Function.hpp"

namespace hal
{
    class WatchdogStm
        : public Watchdog
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            // The early warning fires 63 WWDG clock ticks after a refresh, where the WWDG clock is PCLK1 / (4096 * prescaler)
            uint32_t prescaler{ WWDG_PRESCALER_8 };
            cortex::InterruptPriority interruptPriority{ cortex::InterruptPriority::highest };
        };

        explicit WatchdogStm(const Config& config = Config());

        void Refresh() override;
        infra::Duration EarlyWarningPeriod() const override;
        void Start(const infra::Function<void()>& onEarlyWarning) override;

    private:
        void Interrupt();

        cortex::ImmediateInterruptHandler interruptRegistration;
        WWDG_HandleTypeDef handle{};
        infra::Function<void()> onEarlyWarning;
    };
}

#endif
