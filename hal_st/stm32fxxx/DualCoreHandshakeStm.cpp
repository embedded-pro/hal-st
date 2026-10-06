#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include DEVICE_HEADER
#include <cstdint>

namespace hal
{
    namespace
    {
        constexpr uint32_t semaphoreId = 0;
        constexpr uint32_t cortexM4StopTimeout = 0xfffff;
    }

    bool WaitForCortexM4Stop()
    {
#if defined(DUAL_CORE)
        // The domain clock is requested again as soon as the Cortex-M7 enables a peripheral of the Cortex-M4 domain, so this has to run first
        for (uint32_t timeout = cortexM4StopTimeout; timeout != 0; --timeout)
            if (__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET)
                return true;

        return false;
#else
        return true;
#endif
    }

    void ReleaseCortexM4()
    {
#if defined(DUAL_CORE)
        __HAL_RCC_HSEM_CLK_ENABLE();
        HAL_HSEM_FastTake(semaphoreId);
        HAL_HSEM_Release(semaphoreId, 0);
#endif
    }

    void WaitForCortexM7()
    {
#if defined(DUAL_CORE)
        __HAL_RCC_HSEM_CLK_ENABLE();
        HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(semaphoreId));
        HAL_PWREx_ClearPendingEvent();
        HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFE, PWR_D2_DOMAIN);
        __HAL_HSEM_CLEAR_FLAG(__HAL_HSEM_SEMID_TO_MASK(semaphoreId));
        SystemCoreClockUpdate();
#endif
    }
}
