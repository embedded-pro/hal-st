#pragma once

#include "hal/synchronous_interfaces/SynchronousAdc.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/BoundedVector.hpp"
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

namespace hal
{
    class SynchronousAdcStm
        : public SynchronousAdc
    {
    public:
        static constexpr std::size_t maxSamples{ 8 };

#if defined(LL_ADC_OVS_GRP_REGULAR_CONTINUED) && defined(LL_ADC_OVS_RATIO_2)
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
#endif

        struct Config
        {
            constexpr Config()
            {}

            uint32_t samplingTime{ detail::AdcStmChannelConfig{}.samplingTime };

#if defined(LL_ADC_OVS_GRP_REGULAR_CONTINUED) && defined(LL_ADC_OVS_RATIO_2)
            std::optional<Oversampling> oversampling;
#endif
        };

        SynchronousAdcStm(GpioPinStm& pin, AdcStm& adc, const Config& config = Config());
        SynchronousAdcStm(AdcStm& adc, uint32_t channel, const Config& config = Config());

        Samples Measure(std::size_t numberOfSamples) override;

    private:
        void ConfigureChannel();
        void ConfigureOversampling();

    private:
        AnalogPinStm analogPin;
        AdcStm& adc;
        Config config;
        infra::BoundedVector<uint16_t>::WithMaxSize<maxSamples> samples;
    };
}
