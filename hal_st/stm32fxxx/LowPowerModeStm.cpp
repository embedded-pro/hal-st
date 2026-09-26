#include "hal_st/stm32fxxx/LowPowerModeStm.hpp"
#include DEVICE_HEADER

namespace hal
{
    LowPowerModeStm::LowPowerModeStm(const infra::Function<void()>& restoreClocksAfterStop)
        : restoreClocksAfterStop(restoreClocksAfterStop)
    {}

    void LowPowerModeStm::Enter(PowerMode mode)
    {
        if (mode == PowerMode::deepSleep)
            Stop();
        else
            Sleep();
    }

    void LowPowerModeStm::Sleep() const
    {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    void LowPowerModeStm::Stop() const
    {
#if defined(STM32WB) || defined(STM32WBA)
        Sleep();
#else
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
        restoreClocksAfterStop();
#endif
    }
}
