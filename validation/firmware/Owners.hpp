#pragma once

#include "services/hil/HilPinPool.hpp"

namespace validation::owners
{
    // 17 is the watchdog warning pin (WatchDogFactory.cpp); EMIL owns the ids below extension
    inline constexpr services::HilOwner i2c = 16;
    inline constexpr services::HilOwner i2cTarget = 18;
    inline constexpr services::HilOwner eeprom = 19;
    inline constexpr services::HilOwner spiSlave = 20;
    inline constexpr services::HilOwner timer = 21;
    inline constexpr services::HilOwner timerPwm = 22;
    inline constexpr services::HilOwner lpTimer = 23;
    inline constexpr services::HilOwner lpTimerPwm = 24;
    inline constexpr services::HilOwner quadSpi = 25;
    inline constexpr services::HilOwner syncGpio = 26;
    inline constexpr services::HilOwner analogInput = 27;
    inline constexpr services::HilOwner lowPower = 28;
    inline constexpr services::HilOwner dma = 29;
    inline constexpr services::HilOwner scaffold = 30;

    static_assert(i2c == services::HilOwners::extension && scaffold < services::HilOwners::last);
}
