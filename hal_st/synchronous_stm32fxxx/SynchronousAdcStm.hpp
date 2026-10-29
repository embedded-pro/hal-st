#pragma once

#include "hal/synchronous_interfaces/SynchronousAdc.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/BoundedVector.hpp"
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

namespace hal
{
    // Polled conversions of one channel, for a converter that no interrupt-driven driver owns. Built from a
    // pin, it owns that pin's analog mode; built from a channel, it leaves the pin to whichever driver owns it,
    // so a second converter can read a pin that a triggered sequence on the first one also samples.
    class SynchronousAdcStm
        : public SynchronousAdc
    {
    public:
        static constexpr std::size_t maxSamples{ 8 };

        enum class Oversampling : uint8_t
        {
            x2,
            x4,
            x8,
            x16,
            x32,
            x64,
            x128,
            x256,
        };

        struct Config
        {
            constexpr Config()
            {}

            uint32_t clockPrescaler{ ADC_CLOCK_SYNC_PCLK_DIV4 };
            uint32_t samplingTime{ ADC_SAMPLETIME_247CYCLES_5 };

            // Each sample is the average of this many conversions, which the converter accumulates and shifts back to 12 bits
            std::optional<Oversampling> oversampling;
        };

        SynchronousAdcStm(uint8_t oneBasedIndex, GpioPinStm& pin, const Config& config = Config());
        SynchronousAdcStm(uint8_t oneBasedIndex, uint32_t channel, const Config& config = Config());
        SynchronousAdcStm(const SynchronousAdcStm&) = delete;
        SynchronousAdcStm& operator=(const SynchronousAdcStm&) = delete;
        ~SynchronousAdcStm();

        Samples Measure(std::size_t numberOfSamples) override;

    private:
        void Initialize(uint32_t channel, const Config& config);

    private:
        uint8_t index;
        std::optional<AnalogPinStm> analogPin;
        ADC_HandleTypeDef handle{};
        infra::BoundedVector<uint16_t>::WithMaxSize<maxSamples> samples;
    };
}
