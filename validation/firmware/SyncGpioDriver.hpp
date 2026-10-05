#pragma once

#include <cstdint>

// SynchronousGpioStm.hpp redefines hal::Port, hal::Drive, hal::Speed and hal::WeakPull of GpioStm.hpp, so only
// SyncGpioDriver.cpp includes it and this interface passes plain numbers: port is the letter offset (A = 0)
namespace validation::syncgpio
{
    inline constexpr uint8_t outputSlots = 4;
    inline constexpr uint8_t peripheralSlots = 4;

    bool Open(uint8_t slot, uint8_t port, uint8_t index, bool openDrain, uint8_t speed);
    void Set(uint8_t slot, bool value);
    bool Latch(uint8_t slot);
    void Close(uint8_t slot);

    bool OpenAf(uint8_t slot, uint8_t port, uint8_t index, uint8_t af);
    void CloseAf(uint8_t slot);
}
