#pragma once

#include <cstdint>
#include <optional>

namespace validation
{
    inline constexpr uint32_t i2cMinimumBus = 20000;
    inline constexpr uint32_t i2cMaximumBus = 400000;

    std::optional<uint32_t> I2cTiming(uint32_t kernelHz, uint32_t busHz);
}
