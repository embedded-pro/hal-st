#include "hal_st/synchronous_stm32fxxx/SynchronousPwmStm.hpp"
#include <array>

#if defined(HAS_PERIPHERAL_TIMER)

namespace hal
{
    SynchronousPwmStm::SynchronousPwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, const Config& config)
        : PwmStmBase(timerOneBasedIndex, channels, dummyPinStm, config)
    {}

    SynchronousPwmStm::SynchronousPwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, GpioPinStm& breakPin, const Config& config)
        : PwmStmBase(timerOneBasedIndex, channels, breakPin, config)
    {}

    void SynchronousPwmStm::SetBaseFrequency(Hertz baseFrequency)
    {
        SetBaseFrequencyImpl(baseFrequency);
    }

    void SynchronousPwmStm::Start(DutyCycle globalDutyCycle)
    {
        const std::array dutyCycles{ globalDutyCycle };
        StartImpl(dutyCycles);
    }

    void SynchronousPwmStm::Start(DutyCycle dutyCycle1, DutyCycle dutyCycle2)
    {
        const std::array dutyCycles{ dutyCycle1, dutyCycle2 };
        StartImpl(dutyCycles);
    }

    void SynchronousPwmStm::Start(DutyCycle dutyCycle1, DutyCycle dutyCycle2, DutyCycle dutyCycle3)
    {
        const std::array dutyCycles{ dutyCycle1, dutyCycle2, dutyCycle3 };
        StartImpl(dutyCycles);
    }

    void SynchronousPwmStm::Start(DutyCycle dutyCycle1, DutyCycle dutyCycle2, DutyCycle dutyCycle3, DutyCycle dutyCycle4)
    {
        const std::array dutyCycles{ dutyCycle1, dutyCycle2, dutyCycle3, dutyCycle4 };
        StartImpl(dutyCycles);
    }

    void SynchronousPwmStm::Stop()
    {
        StopImpl();
    }
}

#endif
