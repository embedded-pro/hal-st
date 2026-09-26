#include "hal_st/stm32fxxx/WatchDogStm.hpp"
#include <chrono>

namespace
{
    constexpr uint32_t wwdgClockDivider = 4096;
    constexpr uint32_t counterTicksUntilEarlyWarning = WWDG_CR_T - WWDG_CR_T_6;
}

namespace hal
{
    WatchDogStm::WatchDogStm(const Config& config)
        : interruptRegistration(WWDG_IRQn, [this]()
              {
                  Interrupt();
              })
    {
        __WWDG_CLK_ENABLE();
        handle.Instance = WWDG;
        handle.Init.Prescaler = config.prescaler;
        handle.Init.Window = WWDG_CR_T;
        handle.Init.Counter = WWDG_CR_T;
#ifdef STM32F7
        handle.Init.EWIMode = WWDG_EWI_ENABLE;
#endif
    }

    infra::Duration WatchDogStm::EarlyWarningPeriod() const
    {
        auto prescaler = 1ull << ((handle.Init.Prescaler & WWDG_CFR_WDGTB) >> WWDG_CFR_WDGTB_Pos);
        auto ticks = counterTicksUntilEarlyWarning * wwdgClockDivider * prescaler;
        return std::chrono::duration_cast<infra::Duration>(std::chrono::microseconds(ticks * 1000000ull / HAL_RCC_GetPCLK1Freq()));
    }

    void WatchDogStm::Start(const infra::Function<void()>& onEarlyWarning)
    {
        this->onEarlyWarning = onEarlyWarning;

        HAL_WWDG_Init(&handle);

        SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos)
#ifndef STM32G0
                     | (0 << SCB_AIRCR_PRIGROUP_Pos)
#endif
            ;
        NVIC_SetPriority(WWDG_IRQn, 0);
        WWDG->CFR |= WWDG_CFR_EWI;
    }

    void WatchDogStm::Refresh()
    {
        HAL_WWDG_Refresh(&handle);
        WWDG->SR = 0;
    }

    void WatchDogStm::Interrupt()
    {
        onEarlyWarning();
    }
}
