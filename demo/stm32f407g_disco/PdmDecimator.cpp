#include "demo/stm32f407g_disco/PdmDecimator.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <limits>

namespace examples
{
    namespace
    {
        // A fourth-order CIC filter has a gain of decimation^4 = 2^24, so dropping 9 bits maps a full-scale bit stream onto the 16-bit range
        constexpr int cicOutputShift = 9;
        constexpr int dcBlockerShift = 8;
    }

    uint16_t PdmDecimator::Decimation() const
    {
        return decimationFactor;
    }

    void PdmDecimator::Reset(uint8_t channels, uint32_t)
    {
        really_assert(channels == 1);

        integrators.fill(0);
        combDelays.fill(0);
        bitsInFrame = 0;
        previousCic = 0;
        secondPreviousCic = 0;
        previousFiltered = 0;
        previousOutput = 0;
    }

    std::size_t PdmDecimator::MaxSamples(std::size_t wordCount) const
    {
        return (wordCount * 16 + decimationFactor - 1) / decimationFactor;
    }

    std::size_t PdmDecimator::Convert(infra::MemoryRange<const int16_t> words, infra::MemoryRange<int16_t> samples)
    {
        std::size_t produced = 0;

        for (int16_t word : words)
        {
            const uint16_t bits = static_cast<uint16_t>(word);

            for (int bit = 15; bit >= 0; --bit)
            {
                PushBit(((bits >> bit) & 1) != 0);

                if (bitsInFrame == decimationFactor)
                {
                    bitsInFrame = 0;
                    really_assert(produced != samples.size());
                    samples[produced++] = Finish();
                }
            }
        }

        return produced;
    }

    void PdmDecimator::PushBit(bool bit)
    {
        uint32_t input = bit ? 1u : static_cast<uint32_t>(-1);

        // The integrators wrap on purpose: the combs undo the wrap as long as the register is wider than the filter gain
        for (uint32_t& integrator : integrators)
        {
            integrator += input;
            input = integrator;
        }

        ++bitsInFrame;
    }

    int16_t PdmDecimator::Finish()
    {
        uint32_t value = integrators.back();

        for (uint32_t& delay : combDelays)
        {
            const uint32_t difference = value - delay;
            delay = value;
            value = difference;
        }

        const int32_t cic = static_cast<int32_t>(value) >> cicOutputShift;

        // [-1 6 -1] / 4 lifts the roll-off of the CIC towards the top of the band
        const int32_t compensated = (6 * previousCic - cic - secondPreviousCic) / 4;
        secondPreviousCic = previousCic;
        previousCic = cic;

        // High-pass at about 25 Hz removes the DC offset of the microphone
        const int32_t output = compensated - previousFiltered + previousOutput - (previousOutput >> dcBlockerShift);
        previousFiltered = compensated;
        previousOutput = std::clamp<int32_t>(output, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());

        return static_cast<int16_t>(previousOutput);
    }
}
