#include "validation/firmware/PeripheralClocks.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        bool IsOnApb2(const TIM_TypeDef* instance)
        {
#if defined(TIM1)
            if (instance == TIM1)
                return true;
#endif
#if defined(TIM16)
            if (instance == TIM16)
                return true;
#endif
#if defined(TIM17)
            if (instance == TIM17)
                return true;
#endif
            return false;
        }
    }

    bool TimerExists(uint8_t timer)
    {
        return timer >= 1 && timer <= hal::peripheralTimer.size() && hal::peripheralTimer[timer - 1] != nullptr;
    }

    uint32_t TimerClock(uint8_t timer)
    {
        const auto hclk = HAL_RCC_GetHCLKFreq();
        const auto peripheralClock = IsOnApb2(hal::peripheralTimer[timer - 1]) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();

        if (peripheralClock == 0)
            return hclk;

        return hclk / peripheralClock == 1 ? peripheralClock : peripheralClock * 2;
    }
}
