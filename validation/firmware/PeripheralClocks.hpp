#pragma once

#include <cstdint>

namespace validation
{
    bool TimerExists(uint8_t timer);
    uint32_t TimerClock(uint8_t timer);

    bool I2cExists(uint8_t index);
    uint32_t I2cKernelClock(uint8_t index);

    bool SpiExists(uint8_t index);
    bool SpiLimited(uint8_t index);

    bool LpTimerExists(uint8_t index);
    uint32_t LpTimerClock(uint8_t index);
}
