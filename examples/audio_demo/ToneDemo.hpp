#pragma once

#include "hal/interfaces/AudioOutput.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace examples
{
    class ToneDemo
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            hal::AudioFormat format{ 48000, 2 };
            uint32_t frequencyInHertz{ 440 };
            uint8_t volumePercent{ 30 };
        };

        explicit ToneDemo(hal::AudioOutput& output, const Config& config = Config());
        ToneDemo(const ToneDemo& other) = delete;
        ToneDemo& operator=(const ToneDemo& other) = delete;
        ~ToneDemo();

        void Start();
        void Stop();

        uint32_t Underruns() const;

    private:
        void Fill(hal::AudioOutput::Samples toFill);

    private:
        static constexpr std::size_t tableSize = 256;

        hal::AudioOutput& output;
        Config config;
        std::array<int16_t, tableSize> sine;
        uint32_t phase{ 0 };
        uint32_t phaseStep;
        uint32_t underruns{ 0 };
    };
}
