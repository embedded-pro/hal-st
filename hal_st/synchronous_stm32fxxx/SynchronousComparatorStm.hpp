#pragma once

#include "hal/synchronous_interfaces/SynchronousAnalogComparator.hpp"
#include "hal_st/stm32fxxx/ComparatorStm.hpp"

#if defined(HAS_PERIPHERAL_COMP)

namespace hal
{
    class SynchronousComparatorStm
        : public ComparatorBaseStm
        , public SynchronousAnalogComparator
    {
    public:
        SynchronousComparatorStm(uint8_t oneBasedIndex, GpioPinStm& inputPlus, GpioPinStm& inputMinus, GpioPinStm& output = dummyPinStm, const Config& config = Config());

        bool GetOutput() const override;
    };
}

#endif
