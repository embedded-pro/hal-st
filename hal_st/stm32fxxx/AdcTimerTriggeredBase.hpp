#pragma once

#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/TimerStm.hpp"
#include <cstdint>

#if defined(HAS_PERIPHERAL_TIMER)

namespace hal
{
    class AdcTimerTriggeredBase
    {
    protected:
        AdcTimerTriggeredBase(AdcStm& adc, uint8_t oneBasedTimerIndex, TimerBaseStm::Timing timing);

        void ReconfigureTrigger();
        void StartTimer();
        void StopTimer();

    private:
        AdcStm& adc;
        FreeRunningTimerStm timer;
        uint8_t timerIndex;
    };
}

#endif
