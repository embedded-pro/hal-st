#ifndef HAL_WATCHDOG_STM_HPP
#define HAL_WATCHDOG_STM_HPP

#include DEVICE_HEADER
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/Watchdog.hpp"

namespace hal
{
    class WatchDogStm
        : public Watchdog
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            // WWDG clock (Hz) = PCLK1 / (4096 * Prescaler)                     --> 54Mhz / 32768 = 1728 Hz
            // WWDG timeout (mS) = 1000 * Counter / WWDG clock                  -->  73 ms
            // WWDG Counter refresh is allowed between the following limits :
            // min time (mS) = 1000 * (Counter _ Window) / WWDG clock           --> 0
            // max time (mS) = 1000 * (Counter _ 0x40) / WWDG clock             --> 36 ms
            uint32_t prescaler{ WWDG_PRESCALER_8 };
        };

        explicit WatchDogStm(const Config& config = Config());

        infra::Duration EarlyWarningPeriod() const override;
        void Start(const infra::Function<void()>& onEarlyWarning) override;
        void Refresh() override;
        void Interrupt();

    private:
        cortex::ImmediateInterruptHandler interruptRegistration;
        WWDG_HandleTypeDef handle;
        infra::Function<void()> onEarlyWarning;
    };
}

#endif
