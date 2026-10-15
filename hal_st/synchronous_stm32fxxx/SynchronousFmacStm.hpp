#pragma once

#include "hal_st/stm32fxxx/FmacStm.hpp"
#include <optional>

#if defined(HAS_PERIPHERAL_FMAC)

namespace hal
{
    class SynchronousFmacStm
        : public FmacBaseStm
    {
    public:
        explicit SynchronousFmacStm(const Config& config = Config());

        std::optional<bool> Filter(infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> output);
    };
}

#endif
