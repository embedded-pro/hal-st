#ifndef HAL_LOW_POWER_MODE_STM_HPP
#define HAL_LOW_POWER_MODE_STM_HPP

#include "hal/interfaces/LowPowerMode.hpp"
#include "infra/util/Function.hpp"

namespace hal
{
    class LowPowerModeStm
        : public LowPowerMode
    {
    public:
        explicit LowPowerModeStm(const infra::Function<void()>& restoreClocksAfterStop);

        void Enter(PowerMode mode) override;

    private:
        void Sleep() const;
        void Stop() const;

        infra::Function<void()> restoreClocksAfterStop;
    };
}

#endif
