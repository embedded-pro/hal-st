#include "examples/audio_demo/ToneDemo.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <cmath>

namespace examples
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double fullScale = 32767.0;
        constexpr uint64_t phaseRange = 1ull << 32;
        constexpr uint32_t tableShift = 24;
    }

    ToneDemo::ToneDemo(hal::AudioOutput& output, const Config& config)
        : output(output)
        , config(config)
        , phaseStep(static_cast<uint32_t>(phaseRange * config.frequencyInHertz / config.format.sampleRate))
    {
        really_assert(config.volumePercent <= 100);
        really_assert(config.frequencyInHertz < config.format.sampleRate / 2);

        for (std::size_t index = 0; index != sine.size(); ++index)
            sine[index] = static_cast<int16_t>(std::sin(2.0 * pi * static_cast<double>(index) / static_cast<double>(tableSize)) * fullScale);

        output.SetVolume(config.volumePercent);
    }

    ToneDemo::~ToneDemo()
    {
        Stop();
    }

    void ToneDemo::Start()
    {
        output.Start(
            config.format, [this](hal::AudioOutput::Samples toFill)
            {
                Fill(toFill);
            },
            [this]()
            {
                ++underruns;
            });
    }

    void ToneDemo::Stop()
    {
        output.Stop();
    }

    uint32_t ToneDemo::Underruns() const
    {
        return underruns;
    }

    void ToneDemo::Fill(hal::AudioOutput::Samples toFill)
    {
        for (std::size_t frame = 0; frame + config.format.channels <= toFill.size(); frame += config.format.channels)
        {
            const int16_t sample = sine[phase >> tableShift];
            phase += phaseStep;

            for (uint8_t channel = 0; channel != config.format.channels; ++channel)
                toFill[frame + channel] = sample;
        }
    }
}
