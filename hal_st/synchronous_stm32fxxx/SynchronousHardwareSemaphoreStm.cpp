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
        while (HAL_HSEM_FastTake(static_cast<uint32_t>(semaphore)) != HAL_OK)
        {
        }
    }

    void SynchronousHardwareSemaphoreMasterStm::Release(hal::Semaphore semaphore) const
    {
        HAL_HSEM_Release(static_cast<uint32_t>(semaphore), 0);
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
