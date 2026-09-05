#include DEVICE_HEADER
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/cortex_m/SystemTickTimerService.hpp"
#include <chrono>
#include <cstdint>

extern "C"
{
    // Avoid the SysTick handler from being initialised by HAL_Init
    HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
    {
        return HAL_OK;
    }

    uint32_t HAL_GetTick()
    {
        if (hal::cortex::SystemTickTimerService::InstanceSet())
            return std::chrono::duration_cast<std::chrono::milliseconds>(hal::cortex::SystemTickTimerService::Instance().Now().time_since_epoch()).count();
        else
            return 0;
    }

    [[gnu::weak]] void Default_Handler_Forwarded()
    {
        hal::cortex::InterruptTable::Instance().Invoke(hal::cortex::ActiveInterrupt());
    }
}
