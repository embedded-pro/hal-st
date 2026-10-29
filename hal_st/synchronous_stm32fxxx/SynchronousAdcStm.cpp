#include "hal_st/synchronous_stm32fxxx/SynchronousAdcStm.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>

namespace
{
    constexpr uint32_t conversionTimeoutMs = 10;

    constexpr std::array<uint32_t, 8> oversamplingRatios{ {
        ADC_OVERSAMPLING_RATIO_2,
        ADC_OVERSAMPLING_RATIO_4,
        ADC_OVERSAMPLING_RATIO_8,
        ADC_OVERSAMPLING_RATIO_16,
        ADC_OVERSAMPLING_RATIO_32,
        ADC_OVERSAMPLING_RATIO_64,
        ADC_OVERSAMPLING_RATIO_128,
        ADC_OVERSAMPLING_RATIO_256,
    } };

    constexpr std::array<uint32_t, 8> oversamplingShifts{ {
        ADC_RIGHTBITSHIFT_1,
        ADC_RIGHTBITSHIFT_2,
        ADC_RIGHTBITSHIFT_3,
        ADC_RIGHTBITSHIFT_4,
        ADC_RIGHTBITSHIFT_5,
        ADC_RIGHTBITSHIFT_6,
        ADC_RIGHTBITSHIFT_7,
        ADC_RIGHTBITSHIFT_8,
    } };
}

namespace hal
{
    SynchronousAdcStm::SynchronousAdcStm(uint8_t oneBasedIndex, GpioPinStm& pin, const Config& config)
        : index(static_cast<uint8_t>(oneBasedIndex - 1))
        , analogPin(std::in_place, pin)
    {
        Initialize(analogPin->AdcChannel(oneBasedIndex), config);
    }

    SynchronousAdcStm::SynchronousAdcStm(uint8_t oneBasedIndex, uint32_t channel, const Config& config)
        : index(static_cast<uint8_t>(oneBasedIndex - 1))
    {
        Initialize(channel, config);
    }

    SynchronousAdcStm::~SynchronousAdcStm()
    {
        HAL_ADC_DeInit(&handle);
        DisableClockAdc(index);
    }

    void SynchronousAdcStm::Initialize(uint32_t channel, const Config& config)
    {
        really_assert(index < peripheralAdc.size());

        EnableClockAdc(index);

        handle.Instance = peripheralAdc[index];
        handle.Init.ClockPrescaler = config.clockPrescaler;
        handle.Init.Resolution = ADC_RESOLUTION_12B;
        handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
        handle.Init.GainCompensation = 0;
        handle.Init.ScanConvMode = ADC_SCAN_DISABLE;
        handle.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
        handle.Init.LowPowerAutoWait = DISABLE;
        handle.Init.ContinuousConvMode = DISABLE;
        handle.Init.NbrOfConversion = 1;
        handle.Init.DiscontinuousConvMode = DISABLE;
        handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
        handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
        handle.Init.DMAContinuousRequests = DISABLE;
        handle.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
        handle.Init.OversamplingMode = config.oversampling ? ENABLE : DISABLE;

        if (config.oversampling)
        {
            const auto selected = static_cast<std::size_t>(*config.oversampling);
            handle.Init.Oversampling.Ratio = oversamplingRatios[selected];
            handle.Init.Oversampling.RightBitShift = oversamplingShifts[selected];
            handle.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
            handle.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
        }

        auto result = HAL_ADC_Init(&handle);
        really_assert(result == HAL_OK);

        result = HAL_ADCEx_Calibration_Start(&handle, ADC_SINGLE_ENDED);
        really_assert(result == HAL_OK);

        ADC_ChannelConfTypeDef channelConfig{};
        channelConfig.Channel = channel;
        channelConfig.Rank = ADC_REGULAR_RANK_1;
        channelConfig.SamplingTime = config.samplingTime;
        channelConfig.SingleDiff = ADC_SINGLE_ENDED;
        channelConfig.OffsetNumber = ADC_OFFSET_NONE;
        channelConfig.Offset = 0;

        result = HAL_ADC_ConfigChannel(&handle, &channelConfig);
        really_assert(result == HAL_OK);
    }

    SynchronousAdcStm::Samples SynchronousAdcStm::Measure(std::size_t numberOfSamples)
    {
        really_assert(numberOfSamples <= maxSamples);

        samples.clear();

        while (samples.size() != numberOfSamples)
        {
            auto result = HAL_ADC_Start(&handle);
            really_assert(result == HAL_OK);

            result = HAL_ADC_PollForConversion(&handle, conversionTimeoutMs);
            really_assert(result == HAL_OK);

            samples.push_back(static_cast<uint16_t>(HAL_ADC_GetValue(&handle)));

            result = HAL_ADC_Stop(&handle);
            really_assert(result == HAL_OK);
        }

        return infra::MakeRange(samples);
    }
}
