#include "hal_st/stm32fxxx/AdcDmaMultiChannelStm.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <variant>
#include DEVICE_HEADER

namespace
{
    constexpr std::array<uint32_t, hal::AdcDmaMultiChannelStmBase::MaxChannels> rank = {
        ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3, ADC_REGULAR_RANK_4,
        ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6, ADC_REGULAR_RANK_7, ADC_REGULAR_RANK_8,
#if defined(ADC_REGULAR_RANK_9)
        ADC_REGULAR_RANK_9, ADC_REGULAR_RANK_10, ADC_REGULAR_RANK_11, ADC_REGULAR_RANK_12,
        ADC_REGULAR_RANK_13, ADC_REGULAR_RANK_14, ADC_REGULAR_RANK_15, ADC_REGULAR_RANK_16
#endif
    };

    constexpr std::array<uint32_t, hal::AdcDmaMultiChannelStmBase::MaxChannels> sequencerLength = {
        LL_ADC_REG_SEQ_SCAN_DISABLE, LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_3RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_4RANKS,
        LL_ADC_REG_SEQ_SCAN_ENABLE_5RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_6RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_7RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_8RANKS,
#if defined(ADC_REGULAR_RANK_9)
        LL_ADC_REG_SEQ_SCAN_ENABLE_9RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_10RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_11RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_12RANKS,
        LL_ADC_REG_SEQ_SCAN_ENABLE_13RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_14RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_15RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_16RANKS
#endif
    };

#if defined(ADC_SMPR_SMP1)
    constexpr std::array<uint32_t, 2> samplingTimeCommon = { ADC_SAMPLINGTIME_COMMON_1, ADC_SAMPLINGTIME_COMMON_2 };

    uint32_t SelectSamplingTimeCommon(ADC_TypeDef* adc, std::array<std::optional<uint32_t>, samplingTimeCommon.size()>& assigned, uint32_t samplingTime)
    {
        really_assert(IS_ADC_SAMPLE_TIME(samplingTime));

        auto group = std::find_if(assigned.begin(), assigned.end(), [samplingTime](const auto& assignedTime)
            {
                return !assignedTime || *assignedTime == samplingTime;
            });
        really_assert(group != assigned.end());

        auto common = samplingTimeCommon[std::distance(assigned.begin(), group)];
        if (!*group)
        {
            *group = samplingTime;
            LL_ADC_SetSamplingTimeCommonChannels(adc, common, samplingTime);
        }

        return common;
    }
#endif
}

namespace hal
{
    AdcDmaMultiChannelStmBase::AdcDmaMultiChannelStmBase(infra::MemoryRange<uint16_t> buffer, infra::MemoryRange<AnalogPinStm> analogPins, AdcStm& adc, DmaStm::ReceiveStream& receiveStream, OneShot)
        : buffer{ buffer }
        , analogPins{ analogPins }
        , adc{ adc }
        , dmaStream(std::in_place_type_t<ReceiveDmaChannel>{}, receiveStream, &adc.Handle().Instance->DR, sizeof(uint16_t), [this]()
              {
                  TransferDone();
              },
              DmaStm::StreamInterruptHandler::immediate)
    {
        Initialize();
        LL_ADC_REG_SetDMATransfer(adc.Handle().Instance, LL_ADC_REG_DMA_TRANSFER_LIMITED);
    }

    AdcDmaMultiChannelStmBase::AdcDmaMultiChannelStmBase(infra::MemoryRange<uint16_t> buffer, infra::MemoryRange<AnalogPinStm> analogPins, AdcStm& adc, DmaStm::ReceiveStream& receiveStream, Unlimited)
        : buffer{ buffer }
        , analogPins{ analogPins }
        , adc{ adc }
        , dmaStream(std::in_place_type_t<CircularReceiveDmaChannel>{}, receiveStream, &adc.Handle().Instance->DR, sizeof(uint16_t), infra::emptyFunction, [this]()
              {
                  TransferDone();
              })
    {
        Initialize();
        LL_ADC_REG_SetDMATransfer(adc.Handle().Instance, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
    }

    void AdcDmaMultiChannelStmBase::Initialize()
    {
#ifdef ADC_DIFFERENTIAL_ENDED
        // single ended calibration is already done by AdcStm
        auto result = HAL_ADCEx_Calibration_Start(&adc.Handle(), ADC_DIFFERENTIAL_ENDED);
        assert(result == HAL_OK);
#elif defined(IS_ADC_CALFACT)
        auto result = HAL_ADCEx_Calibration_Start(&adc.Handle());
        assert(result == HAL_OK);
#endif
        adc.EnableOverrunInterrupt();
    }

    void AdcDmaMultiChannelStmBase::Measure(const infra::Function<void(Samples)>& onDone)
    {
        this->onDone = onDone;

        auto result = LL_ADC_REG_IsConversionOngoing(adc.Handle().Instance);
        assert(result == 0);

        result = ADC_Enable(&adc.Handle());
        assert(result == HAL_OK);

        __HAL_ADC_CLEAR_FLAG(&adc.Handle(), (ADC_FLAG_EOC | ADC_FLAG_EOS | ADC_FLAG_OVR));

        // clang-format off
        std::visit([this](auto& v) { v.StartReceive(buffer); }, dmaStream);
        // clang-format on

        LL_ADC_REG_StartConversion(adc.Handle().Instance);
    }

    void AdcDmaMultiChannelStmBase::Stop()
    {
        if (LL_ADC_REG_IsConversionOngoing(adc.Handle().Instance))
            LL_ADC_REG_StopConversion(adc.Handle().Instance);

        auto result = ADC_Disable(&adc.Handle());
        assert(result == HAL_OK);

        // clang-format off
        std::visit([this](auto& v) { v.StopTransfer(); }, dmaStream);
        // clang-format on
    }

    void AdcDmaMultiChannelStmBase::ConfigureChannels(infra::MemoryRange<const detail::AdcStmChannelConfig> configs)
    {
        really_assert(!analogPins.empty() && analogPins.size() <= rank.size());

        ADC_ChannelConfTypeDef channelConfig = { 0 };
#ifdef ADC_OFFSET_NONE
        channelConfig.OffsetNumber = ADC_OFFSET_NONE;
        channelConfig.Offset = 0;
#endif
#if defined(ADC_CFGR1_CHSELRMOD)
        // HAL_ADC_ConfigChannel only writes ranks up to NbrOfConversion into CHSELR
        adc.Handle().Init.ScanConvMode = ADC_SCAN_ENABLE;
        adc.Handle().Init.NbrOfConversion = analogPins.size();
#endif
#if defined(ADC_SMPR_SMP1)
        std::array<std::optional<uint32_t>, samplingTimeCommon.size()> assignedSamplingTimes;
#endif

        for (std::size_t i = 0; i != analogPins.size(); ++i)
        {
            channelConfig.Channel = adc.Channel(analogPins[i]);

            // The sampling time is selected per channel, not per rank
            for (std::size_t j = 0; j != i; ++j)
                really_assert(adc.Channel(analogPins[j]) != channelConfig.Channel || configs[j].samplingTime == configs[i].samplingTime);

#if defined(ADC_SMPR_SMP1)
            channelConfig.SamplingTime = SelectSamplingTimeCommon(adc.Handle().Instance, assignedSamplingTimes, configs[i].samplingTime);
#else
            channelConfig.SamplingTime = configs[i].samplingTime;
#endif
#ifdef ADC_SINGLE_ENDED
            channelConfig.SingleDiff = configs[i].differential ? ADC_DIFFERENTIAL_ENDED : ADC_SINGLE_ENDED;
#endif
            channelConfig.Rank = rank[i];
            auto result = HAL_ADC_ConfigChannel(&adc.Handle(), &channelConfig);
            assert(result == HAL_OK);
        }
        LL_ADC_REG_SetSequencerLength(adc.Handle().Instance, sequencerLength[analogPins.size() - 1]);
    }

    void AdcDmaMultiChannelStmBase::TransferDone()
    {
        if (std::holds_alternative<ReceiveDmaChannel>(dmaStream))
        {
            auto result = ADC_Disable(&adc.Handle());
            assert(result == HAL_OK);
        }
        if (this->onDone)
            onDone(buffer);
    }
}
