#pragma once

#include "hal_st/stm32fxxx/CordicStm.hpp"

#if defined(HAS_PERIPHERAL_CORDIC)

namespace hal
{
    class SynchronousCordicStm
        : public CordicBaseStm
    {
    public:
        explicit SynchronousCordicStm(const Config& config = Config());

        bool Calculate(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results);
        bool CalculateZeroOverhead(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results);
    };
}

#endif
