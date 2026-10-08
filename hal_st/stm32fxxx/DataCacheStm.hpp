#pragma once

#include <cstddef>
#include <cstdint>
#include DEVICE_HEADER

namespace hal
{
    constexpr uintptr_t dataCacheLineSize = 32;

    inline bool DataCacheEnabled()
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        return (SCB->CCR & SCB_CCR_DC_Msk) != 0;
#else
        return false;
#endif
    }

    inline void CleanDataCache([[maybe_unused]] const void* address, [[maybe_unused]] std::size_t size)
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        if (DataCacheEnabled())
            SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(const_cast<void*>(address)), static_cast<int32_t>(size));
#endif
    }

    inline void InvalidateDataCache([[maybe_unused]] void* address, [[maybe_unused]] std::size_t size)
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        if (DataCacheEnabled())
            SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(address), static_cast<int32_t>(size));
#endif
    }
}
