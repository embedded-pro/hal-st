#pragma once

#include "hal/cortex_m/DataWatchpointAndTrace.hpp"
#include <cstdint>
#include DEVICE_HEADER

namespace validation
{
    // CYCCNT stops in Sleep (the core clock is gated), so a sleep window is timed with the scaffold timer instead
    class Stopwatch
    {
    public:
        Stopwatch();

        void Start();
        uint32_t ElapsedUs() const;

    private:
        hal::cortex::DataWatchpointAndTrace dwt;
        uint32_t start = 0;
    };

    ////    Implementation    ////

    inline Stopwatch::Stopwatch()
    {
        // DataWatchpointAndTrace::Start() zeroes the one shared CYCCNT, which would corrupt another running stopwatch
        static bool counting = false;

        if (!counting)
        {
            dwt.Start();
            counting = true;
        }
    }

    inline void Stopwatch::Start()
    {
        start = dwt.Cycles();
    }

    inline uint32_t Stopwatch::ElapsedUs() const
    {
        return static_cast<uint32_t>(static_cast<uint64_t>(dwt.Cycles() - start) * 1'000'000 / SystemCoreClock);
    }
}
