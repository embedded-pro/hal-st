#pragma once

#include "services/hil/HilPinPool.hpp"
#include "services/hil/HilStatus.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    // PWM, encoder and timer-triggered ADC each need a whole timer; this keeps them from sharing one
    class TimerAllocation
    {
    public:
        services::HilStatus Claim(uint8_t timer, services::HilOwner owner);
        void Release(uint8_t timer, services::HilOwner owner);
        std::optional<services::HilOwner> Owner(uint8_t timer) const;

    private:
        std::array<std::optional<services::HilOwner>, 32> owners;
    };
}
