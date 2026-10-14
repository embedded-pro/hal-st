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
#if defined(TIM8)
            if (instance == TIM8)
                return true;
#endif
#if defined(TIM15)
            if (instance == TIM15)
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
#if defined(TIM20)
            if (instance == TIM20)
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

    bool I2cExists(uint8_t index)
    {
        return index >= 1 && index <= hal::peripheralI2c.size() && hal::peripheralI2c[index - 1] != nullptr;
    }

    uint32_t I2cKernelClock(uint8_t index)
    {
        switch (index)
        {
            case 1:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C1);
#if defined(RCC_PERIPHCLK_I2C2)
            case 2:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C2);
#endif
            case 3:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C3);
#if defined(RCC_PERIPHCLK_I2C4)
            case 4:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C4);
#endif
            default:
                return 0;
        }
    }

    bool SpiExists(uint8_t index)
    {
        return index >= 1 && index <= hal::peripheralSpi.size() && hal::peripheralSpi[index - 1] != nullptr;
    }

    bool SpiLimited(uint8_t index)
    {
#if defined(IS_SPI_LIMITED_INSTANCE)
        return SpiExists(index) && IS_SPI_LIMITED_INSTANCE(hal::peripheralSpi[index - 1]);
#else
        return false;
#endif
    }

    bool LpTimerExists(uint8_t index)
    {
#if defined(HAS_PERIPHERAL_LPTIMER)
        return index >= 1 && index <= hal::peripheralLpTimer.size() && hal::peripheralLpTimer[index - 1] != nullptr;
#else
        return false;
#endif
    }

    uint32_t LpTimerClock(uint8_t index)
    {
        switch (index)
        {
            case 1:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_LPTIM1);
#if defined(RCC_PERIPHCLK_LPTIM2)
            case 2:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_LPTIM2);
#endif
            default:
                return 0;
        }
    }
}
