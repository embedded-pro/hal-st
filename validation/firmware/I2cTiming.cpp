#include "validation/firmware/I2cTiming.hpp"
#include <algorithm>

namespace validation
{
    namespace
    {
        struct Mode
        {
            int64_t hddatMin;
            int64_t vddatMax;
            int64_t sudatMin;
            int64_t lsclMin;
            int64_t hsclMin;
            int64_t rise;
            int64_t fall;
        };

        struct Candidate
        {
            int64_t period;
            uint32_t prescaler;
            uint32_t scldel;
            uint32_t sdadel;
            uint32_t sclh;
            uint32_t scll;
        };

        constexpr int64_t picosecondsPerSecond = 1'000'000'000'000;
        constexpr Mode standardMode{ 0, 3'450'000, 250'000, 4'700'000, 4'000'000, 640'000, 20'000 };
        constexpr Mode fastMode{ 0, 900'000, 100'000, 1'300'000, 600'000, 250'000, 100'000 };
        constexpr int64_t analogFilterMin = 50'000;
        constexpr int64_t analogFilterMax = 260'000;
        constexpr uint32_t fieldMax = 15;
        constexpr int64_t periodMax = 255;
        constexpr uint32_t standardModeMaximumBus = 100'000;

        int64_t FloorDivide(int64_t numerator, int64_t denominator)
        {
            const auto quotient = numerator / denominator;
            return quotient * denominator > numerator ? quotient - 1 : quotient;
        }

        int64_t CeilDivide(int64_t numerator, int64_t denominator)
        {
            return -FloorDivide(-numerator, denominator);
        }

        std::optional<Candidate> Evaluate(const Mode& mode, int64_t clock, int64_t target, uint32_t prescaler)
        {
            const int64_t prescaled = (prescaler + 1) * clock;
            const int64_t sdadelMin = std::max<int64_t>(mode.fall + mode.hddatMin - analogFilterMin - 3 * clock, 0);
            const int64_t sdadelMax = std::max<int64_t>(mode.vddatMax - mode.rise - analogFilterMax - 4 * clock, 0);
            const int64_t scldelMin = mode.rise + mode.sudatMin;

            uint32_t scldel = 0;
            while (scldel <= fieldMax && (scldel + 1) * prescaled < scldelMin)
                ++scldel;
            if (scldel > fieldMax)
                return std::nullopt;

            uint32_t sdadel = 0;
            for (; sdadel <= fieldMax; ++sdadel)
            {
                const int64_t delay = (sdadel * (prescaler + 1) + 1) * clock;
                if (delay >= sdadelMin && delay <= sdadelMax)
                    break;
            }
            if (sdadel > fieldMax)
                return std::nullopt;

            const int64_t sync = analogFilterMin + 2 * clock;
            const int64_t scllMin = std::max<int64_t>(0, FloorDivide(mode.lsclMin - sync, prescaled));
            const int64_t sclhMin = std::max<int64_t>(0, CeilDivide(mode.hsclMin - sync, prescaled) - 1);
            const int64_t fixed = 2 * sync + mode.fall;
            const int64_t units = std::max(scllMin + sclhMin + 2, CeilDivide(target - fixed, prescaled));
            const int64_t extra = units - (scllMin + sclhMin + 2);
            const int64_t scll = scllMin + (extra + 1) / 2;
            const int64_t sclh = sclhMin + extra / 2;
            if (scll > periodMax || sclh > periodMax)
                return std::nullopt;

            return Candidate{ fixed + units * prescaled, prescaler, scldel, sdadel, static_cast<uint32_t>(sclh), static_cast<uint32_t>(scll) };
        }
    }

    std::optional<uint32_t> I2cTiming(uint32_t kernelHz, uint32_t busHz)
    {
        if (kernelHz == 0 || busHz < i2cMinimumBus || busHz > i2cMaximumBus)
            return std::nullopt;

        const Mode& mode = busHz <= standardModeMaximumBus ? standardMode : fastMode;
        const int64_t clock = picosecondsPerSecond / kernelHz;
        const int64_t target = picosecondsPerSecond / busHz;

        std::optional<Candidate> best;
        for (uint32_t prescaler = 0; prescaler <= fieldMax; ++prescaler)
        {
            auto candidate = Evaluate(mode, clock, target, prescaler);
            if (candidate && (!best || candidate->period < best->period))
                best = candidate;
        }

        if (!best)
            return std::nullopt;

        return (best->prescaler << 28) | (best->scldel << 20) | (best->sdadel << 16) | (best->sclh << 8) | best->scll;
    }
}
