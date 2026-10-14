#include "hal_st/synchronous_stm32fxxx/SynchronousComparatorStm.hpp"

#if defined(HAS_PERIPHERAL_COMP)

namespace hal
{
    SynchronousComparatorStm::SynchronousComparatorStm(uint8_t oneBasedIndex, GpioPinStm& inputPlus, GpioPinStm& inputMinus, GpioPinStm& output, const Config& config)
        : ComparatorBaseStm(oneBasedIndex, inputPlus, inputMinus, output, config)
    {}

    bool SynchronousComparatorStm::GetOutput() const
    {
        return Output();
    }
}

#endif
