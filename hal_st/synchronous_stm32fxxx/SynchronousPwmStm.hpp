#pragma once

#include "hal/synchronous_interfaces/SynchronousPwm.hpp"
#include "hal_st/stm32fxxx/PwmStm.hpp"

#if defined(HAS_PERIPHERAL_TIMER)

namespace hal
{
    // Same register-level base as PwmStm; see PwmStm.hpp for the configuration surface.
    class SynchronousPwmStm
        : public PwmStmBase
        , public SynchronousSingleChannelPwm
        , public SynchronousTwoChannelsPwm
        , public SynchronousThreeChannelsPwm
        , public SynchronousFourChannelsPwm
    {
    public:
        SynchronousPwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, const Config& config = Config());
        SynchronousPwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, GpioPinStm& breakPin, const Config& config = Config());

        void SetBaseFrequency(Hertz baseFrequency) override;
        void Start(FractionalPercent globalDutyCycle) override;
        void Start(FractionalPercent dutyCycle1, FractionalPercent dutyCycle2) override;
        void Start(FractionalPercent dutyCycle1, FractionalPercent dutyCycle2, FractionalPercent dutyCycle3) override;
        void Start(FractionalPercent dutyCycle1, FractionalPercent dutyCycle2, FractionalPercent dutyCycle3, FractionalPercent dutyCycle4) override;
        void Stop() override;
    };
}

#endif
