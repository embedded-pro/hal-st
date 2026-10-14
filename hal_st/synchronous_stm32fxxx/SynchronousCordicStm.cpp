#include "hal_st/synchronous_stm32fxxx/SynchronousCordicStm.hpp"

#if defined(HAS_PERIPHERAL_CORDIC)

namespace hal
{
    namespace
    {
        constexpr uint32_t timeoutMs = 100;
    }

    SynchronousCordicStm::SynchronousCordicStm(const Config& config)
        : CordicBaseStm(config)
    {}

    bool SynchronousCordicStm::Calculate(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results)
    {
        auto calculations = Calculations(arguments, results);
        return HAL_CORDIC_Calculate(&handle, const_cast<int32_t*>(arguments.begin()), results.begin(), calculations, timeoutMs) == HAL_OK;
    }

    bool SynchronousCordicStm::CalculateZeroOverhead(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results)
    {
        auto calculations = Calculations(arguments, results);
        return HAL_CORDIC_CalculateZO(&handle, const_cast<int32_t*>(arguments.begin()), results.begin(), calculations, timeoutMs) == HAL_OK;
    }
}

#endif
