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
        void Start(Percent globalDutyCycle) override;
        void Start(Percent dutyCycle1, Percent dutyCycle2) override;
        void Start(Percent dutyCycle1, Percent dutyCycle2, Percent dutyCycle3) override;
        void Start(Percent dutyCycle1, Percent dutyCycle2, Percent dutyCycle3, Percent dutyCycle4) override;
        void Stop() override;
    };
}

#endif
