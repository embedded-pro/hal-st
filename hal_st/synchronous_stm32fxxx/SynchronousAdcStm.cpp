#include "hal_st/synchronous_stm32fxxx/SynchronousAdcStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>

#if defined(STM32F7)
extern "C"
{
#include "stm32f7xx_ll_adc.h"
}
#elif defined(STM32H7)
#include "stm32h7xx_ll_adc.h"
#endif

namespace
{
    constexpr uint32_t conversionTimeoutMs = 10;

#if defined(LL_ADC_OVS_GRP_REGULAR_CONTINUED) && defined(LL_ADC_OVS_RATIO_2)
    constexpr std::array<uint32_t, 8> oversamplingRatios{ {
        LL_ADC_OVS_RATIO_2,
        LL_ADC_OVS_RATIO_4,
        LL_ADC_OVS_RATIO_8,
        LL_ADC_OVS_RATIO_16,
        LL_ADC_OVS_RATIO_32,
        LL_ADC_OVS_RATIO_64,
        LL_ADC_OVS_RATIO_128,
        LL_ADC_OVS_RATIO_256,
    } };

    constexpr std::array<uint32_t, 8> oversamplingShifts{ {
        LL_ADC_OVS_SHIFT_RIGHT_1,
        LL_ADC_OVS_SHIFT_RIGHT_2,
        LL_ADC_OVS_SHIFT_RIGHT_3,
        LL_ADC_OVS_SHIFT_RIGHT_4,
        LL_ADC_OVS_SHIFT_RIGHT_5,
        LL_ADC_OVS_SHIFT_RIGHT_6,
        LL_ADC_OVS_SHIFT_RIGHT_7,
        LL_ADC_OVS_SHIFT_RIGHT_8,
    } };
#endif
}

namespace hal
{
    SynchronousAdcStm::SynchronousAdcStm(GpioPinStm& pin, AdcStm& adc, const Config& config)
        : analogPin(pin)
        , adc(adc)
        , config(config)
    {
        HAL_ADC_Stop(&adc.Handle());
        LL_ADC_REG_SetTriggerSource(adc.Handle().Instance, ADC_SOFTWARE_START);
    }

    SynchronousAdcStm::SynchronousAdcStm(AdcStm& adc, uint32_t channel, const Config& config)
        : analogPin(channel)
        , adc(adc)
        , config(config)
    {
        HAL_ADC_Stop(&adc.Handle());
        LL_ADC_REG_SetTriggerSource(adc.Handle().Instance, ADC_SOFTWARE_START);
    }

    SynchronousAdcStm::Samples SynchronousAdcStm::Measure(std::size_t numberOfSamples)
    {
        really_assert(numberOfSamples <= maxSamples);

        ConfigureChannel();
        ConfigureOversampling();

        samples.clear();

        while (samples.size() != numberOfSamples)
        {
            auto result = HAL_ADC_Start(&adc.Handle());
            really_assert(result == HAL_OK);

            result = HAL_ADC_PollForConversion(&adc.Handle(), conversionTimeoutMs);
            really_assert(result == HAL_OK);

            samples.push_back(static_cast<uint16_t>(HAL_ADC_GetValue(&adc.Handle())));
        }

        HAL_ADC_Stop(&adc.Handle());

        return infra::MakeRange(samples);
    }

    void SynchronousAdcStm::ConfigureChannel()
    {
        ADC_ChannelConfTypeDef channelConfig{};
        channelConfig.Channel = adc.Channel(analogPin);
#ifdef ADC_REGULAR_RANK_1
        channelConfig.Rank = ADC_REGULAR_RANK_1;
#else
        channelConfig.Rank = 1;
#endif
#ifdef ADC_SMPR_SMP1
        channelConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
#else
        channelConfig.SamplingTime = config.samplingTime;
#endif
#ifdef ADC_OFFSET_NONE
        channelConfig.OffsetNumber = ADC_OFFSET_NONE;
#endif
#ifdef ADC_SINGLE_ENDED
        channelConfig.SingleDiff = ADC_SINGLE_ENDED;
#endif
#ifdef ADC_OFFSET_1
        channelConfig.Offset = 0;
#endif

        adc.SelectSingleConversion();
        auto result = HAL_ADC_ConfigChannel(&adc.Handle(), &channelConfig);
        really_assert(result == HAL_OK);
    }

    // The oversampler belongs to the converter, not the channel, so every measurement sets it for itself
    void SynchronousAdcStm::ConfigureOversampling()
    {
#if defined(LL_ADC_OVS_GRP_REGULAR_CONTINUED) && defined(LL_ADC_OVS_RATIO_2)
        if (!config.oversampling)
        {
            LL_ADC_SetOverSamplingScope(adc.Handle().Instance, LL_ADC_OVS_DISABLE);
            return;
        }

        const auto selected = static_cast<std::size_t>(*config.oversampling);
        LL_ADC_ConfigOverSamplingRatioShift(adc.Handle().Instance, oversamplingRatios[selected], oversamplingShifts[selected]);
        LL_ADC_SetOverSamplingScope(adc.Handle().Instance, LL_ADC_OVS_GRP_REGULAR_CONTINUED);
#endif
    }
}
