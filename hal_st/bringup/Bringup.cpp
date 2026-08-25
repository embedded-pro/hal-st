#include DEVICE_HEADER
#include "hal/cortex_m/InterruptCortex.hpp"

extern "C"
{
    // Avoid the SysTick handler from being initialised by HAL_Init
    HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
    {
        return HAL_OK;
    }

    [[gnu::weak]] void Default_Handler_Forwarded()
    {
        hal::cortex::InterruptTable::Instance().Invoke(hal::cortex::ActiveInterrupt());
    }
}
