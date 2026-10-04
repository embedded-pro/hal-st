#pragma once

#include "services/hil/HilPinPool.hpp"
#include "services/hil/HilStatus.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    enum class Resource : uint8_t
    {
        lpTimer,
        spi,
        i2c,
        adc,
        dma1,
        dma2,
        hsem,
    };

    // Several command groups reach the same peripheral instance or DMA channel; this keeps them from sharing one
    class ResourceAllocation
    {
    public:
        static constexpr std::size_t indices = 16;

        services::HilStatus Claim(Resource resource, uint8_t index, services::HilOwner owner);
        void Release(Resource resource, uint8_t index, services::HilOwner owner);

    private:
        std::optional<services::HilOwner>& Slot(Resource resource, uint8_t index);

    private:
        std::array<std::array<std::optional<services::HilOwner>, indices>, static_cast<std::size_t>(Resource::hsem) + 1> owners;
    };
}
