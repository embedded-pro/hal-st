#pragma once

#if defined(STM32WB)

#include "hal_st/synchronous_stm32fxxx/SynchronousHardwareSemaphoreStm.hpp"

namespace validation
{
    hal::SynchronousHardwareSemaphoreMasterStm& HsemMaster();
}

#endif
