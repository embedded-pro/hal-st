#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/LtdcStm.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace main_
{
    class DiscoveryLcdPins
    {
    public:
        DiscoveryLcdPins()
        {
            for (const Assignment& assignment : assignments)
            {
                pins.emplace_back(assignment.port, assignment.index);
                signals.emplace_back(hal::LtdcStm::SignalPin{ assignment.signal, pins.back() });
            }
        }

        infra::MemoryRange<const hal::LtdcStm::SignalPin> Signals() const
        {
            return infra::MakeRange(signals);
        }

    private:
        struct Assignment
        {
            hal::PinConfigTypeStm signal;
            hal::Port port;
            uint8_t index;
        };

        static constexpr std::array<Assignment, hal::LtdcStm::maxSignalPins> assignments{ { { hal::PinConfigTypeStm::ltdcClk, hal::Port::I, 14 },
            { hal::PinConfigTypeStm::ltdcHsync, hal::Port::I, 12 },
            { hal::PinConfigTypeStm::ltdcVsync, hal::Port::I, 9 },
            { hal::PinConfigTypeStm::ltdcDe, hal::Port::K, 7 },
            { hal::PinConfigTypeStm::ltdcR0, hal::Port::I, 15 },
            { hal::PinConfigTypeStm::ltdcR1, hal::Port::J, 0 },
            { hal::PinConfigTypeStm::ltdcR2, hal::Port::J, 1 },
            { hal::PinConfigTypeStm::ltdcR3, hal::Port::H, 9 },
            { hal::PinConfigTypeStm::ltdcR4, hal::Port::J, 3 },
            { hal::PinConfigTypeStm::ltdcR5, hal::Port::J, 4 },
            { hal::PinConfigTypeStm::ltdcR6, hal::Port::J, 5 },
            { hal::PinConfigTypeStm::ltdcR7, hal::Port::J, 6 },
            { hal::PinConfigTypeStm::ltdcG0, hal::Port::J, 7 },
            { hal::PinConfigTypeStm::ltdcG1, hal::Port::J, 8 },
            { hal::PinConfigTypeStm::ltdcG2, hal::Port::J, 9 },
            { hal::PinConfigTypeStm::ltdcG3, hal::Port::J, 10 },
            { hal::PinConfigTypeStm::ltdcG4, hal::Port::J, 11 },
            { hal::PinConfigTypeStm::ltdcG5, hal::Port::I, 0 },
            { hal::PinConfigTypeStm::ltdcG6, hal::Port::I, 1 },
            { hal::PinConfigTypeStm::ltdcG7, hal::Port::K, 2 },
            { hal::PinConfigTypeStm::ltdcB0, hal::Port::J, 12 },
            { hal::PinConfigTypeStm::ltdcB1, hal::Port::J, 13 },
            { hal::PinConfigTypeStm::ltdcB2, hal::Port::J, 14 },
            { hal::PinConfigTypeStm::ltdcB3, hal::Port::J, 15 },
            { hal::PinConfigTypeStm::ltdcB4, hal::Port::K, 3 },
            { hal::PinConfigTypeStm::ltdcB5, hal::Port::K, 4 },
            { hal::PinConfigTypeStm::ltdcB6, hal::Port::K, 5 },
            { hal::PinConfigTypeStm::ltdcB7, hal::Port::K, 6 } } };

        infra::BoundedVector<hal::GpioPinStm>::WithMaxSize<hal::LtdcStm::maxSignalPins> pins;
        infra::BoundedVector<hal::LtdcStm::SignalPin>::WithMaxSize<hal::LtdcStm::maxSignalPins> signals;
    };
}
