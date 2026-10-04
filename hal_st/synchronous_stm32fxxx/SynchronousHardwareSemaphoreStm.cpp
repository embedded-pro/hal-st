#include "hal_st/synchronous_stm32fxxx/SynchronousHardwareSemaphoreStm.hpp"
#include "stm32wbxx.h"
#include "stm32wbxx_ll_hsem.h"

namespace hal
{
    SynchronousHardwareSemaphoreMasterStm::SynchronousHardwareSemaphoreMasterStm()
    {
        __HAL_RCC_HSEM_CLK_ENABLE();
    }

    SynchronousHardwareSemaphoreMasterStm::~SynchronousHardwareSemaphoreMasterStm()
    {
        __HAL_RCC_HSEM_CLK_DISABLE();
    }

    void SynchronousHardwareSemaphoreMasterStm::WaitLock(hal::Semaphore semaphore) const
    {
        HSEM->C1IER |= 1 << static_cast<uint32_t>(semaphore);

        while (HAL_HSEM_FastTake(static_cast<uint32_t>(semaphore)) != HAL_OK)
        {
        }
    }

    void SynchronousHardwareSemaphoreMasterStm::Release(hal::Semaphore semaphore) const
    {
        uint32_t mask = 1 << static_cast<uint32_t>(semaphore);
        HSEM->C1ICR = mask;
        HSEM->R[static_cast<uint32_t>(semaphore)] = HSEM_CR_COREID_CURRENT;
        HSEM->C1IER &= ~mask;
    }

    bool SynchronousHardwareSemaphoreMasterStm::IsLockedByCurrentCore(hal::Semaphore semaphore) const
    {
        return LL_HSEM_IsSemaphoreLocked(HSEM, static_cast<uint32_t>(semaphore)) != 0 && LL_HSEM_GetCoreId(HSEM, static_cast<uint32_t>(semaphore)) == LL_HSEM_COREID;
    }

    SynchronousHardwareSemaphoreStm::SynchronousHardwareSemaphoreStm(SynchronousHardwareSemaphoreMasterStm& synchronousHardwareSemaphoreMaster, Semaphore semaphore)
        : synchronousHardwareSemaphoreMaster(synchronousHardwareSemaphoreMaster)
        , semaphore(semaphore)
    {
        synchronousHardwareSemaphoreMaster.WaitLock(semaphore);
    }

    SynchronousHardwareSemaphoreStm::~SynchronousHardwareSemaphoreStm()
    {
        synchronousHardwareSemaphoreMaster.Release(semaphore);
    }
}
