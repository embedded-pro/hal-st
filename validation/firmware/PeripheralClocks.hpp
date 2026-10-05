#pragma once

#include <cstdint>

namespace validation
{
    bool TimerExists(uint8_t timer);
    uint32_t TimerClock(uint8_t timer);
}
