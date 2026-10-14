#include "hal_st/synchronous_stm32fxxx/SynchronousFmacStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_FMAC)

#if defined(STM32G4)
#include "stm32g4xx_ll_fmac.h"
#endif

namespace hal
{
    namespace
    {
        constexpr uint32_t timeoutMs = 100;
    }

    SynchronousFmacStm::SynchronousFmacStm(const Config& config)
        : FmacBaseStm(config)
    {}

    std::optional<bool> SynchronousFmacStm::Filter(infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> output)
    {
        CheckConfigured();
        really_assert(input.size() == output.size() && !input.empty());

        auto start = HAL_GetTick();
        while (!output.empty())
        {
            if (!input.empty() && LL_FMAC_IsActiveFlag_X1FULL(Instance()) == 0)
            {
                LL_FMAC_WriteData(Instance(), static_cast<uint16_t>(input.front()));
                input.pop_front();
            }

            if (LL_FMAC_IsActiveFlag_YEMPTY(Instance()) == 0)
            {
                output.front() = static_cast<int16_t>(LL_FMAC_ReadData(Instance()));
                output.pop_front();
            }
            else if (HAL_GetTick() - start > timeoutMs)
                return std::nullopt;
        }

        return Saturated();
    }
}

#endif
