#include "hal_st/stm32fxxx/WatchdogStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <chrono>

namespace
{
    constexpr uint32_t counterTicksToEarlyWarning = WWDG_CR_T - WWDG_CR_T_6;
    constexpr uint32_t wwdgClockDivider = 4096;
}

namespace hal
{
    WatchdogStm::WatchdogStm(const Config& config)
        : interruptRegistration(WWDG_IRQn, config.interruptPriority, [this]()
              {
                  Interrupt();
              })
    {
        __HAL_RCC_WWDG_CLK_ENABLE();

        handle.Instance = WWDG;
        handle.Init.Prescaler = config.prescaler;
        handle.Init.Window = WWDG_CR_T;
        handle.Init.Counter = WWDG_CR_T;
        handle.Init.EWIMode = WWDG_EWI_ENABLE;
    }

    void WatchdogStm::Refresh()
    {
        HAL_WWDG_Refresh(&handle);
        __HAL_WWDG_CLEAR_FLAG(&handle, WWDG_FLAG_EWIF);
    }

    infra::Duration WatchdogStm::EarlyWarningPeriod() const
    {
        auto prescaler = 1ull << ((handle.Init.Prescaler & WWDG_CFR_WDGTB) >> WWDG_CFR_WDGTB_Pos);
        auto ticks = counterTicksToEarlyWarning * wwdgClockDivider * prescaler;
        return std::chrono::duration_cast<infra::Duration>(std::chrono::microseconds(ticks * 1000000ull / HAL_RCC_GetPCLK1Freq()));
    }

    void WatchdogStm::Start(const infra::Function<void()>& onEarlyWarning)
    {
        this->onEarlyWarning = onEarlyWarning;

        // WWDG cannot be stopped once enabled, only a reset disables it again
        really_assert(HAL_WWDG_Init(&handle) == HAL_OK);
    }

    void WatchdogStm::Interrupt()
    {
        if (!__HAL_WWDG_GET_FLAG(&handle, WWDG_FLAG_EWIF))
            return;

        onEarlyWarning();
    }
}
