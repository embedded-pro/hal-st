#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include DEVICE_HEADER
#include <cstdint>

namespace hal
{
    namespace
    {
        constexpr uint32_t semaphoreId = 0;
        constexpr uint32_t cortexM4StopTimeout = 0xffff;
    }

    void ReleaseCortexM4()
    {
#if defined(DUAL_CORE)
        __HAL_RCC_HSEM_CLK_ENABLE();

        // Releasing before the Cortex-M4 has entered stop mode would lose the notification
        for (uint32_t timeout = cortexM4StopTimeout; __HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET && timeout != 0; --timeout)
        {
        }

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
