#pragma once

#include "drivers/microphones/pdm/PdmToPcm.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace examples
{
    class PdmDecimator
        : public drivers::PdmToPcm
    {
    public:
        static constexpr uint16_t decimationFactor = 64;

        uint16_t Decimation() const override;
        void Reset(uint8_t channels, uint32_t sampleRate) override;
        std::size_t MaxSamples(std::size_t wordCount) const override;
        std::size_t Convert(infra::MemoryRange<const int16_t> words, infra::MemoryRange<int16_t> samples) override;

    private:
        static constexpr std::size_t cicOrder = 4;

        void PushBit(bool bit);
        int16_t Finish();

    private:
        std::array<uint32_t, cicOrder> integrators{};
        std::array<uint32_t, cicOrder> combDelays{};
        uint16_t bitsInFrame = 0;
        int32_t previousCic = 0;
        int32_t secondPreviousCic = 0;
        int32_t previousFiltered = 0;
        int32_t previousOutput = 0;
    };
}
