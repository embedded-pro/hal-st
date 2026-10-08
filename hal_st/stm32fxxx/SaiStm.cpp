#include "hal_st/stm32fxxx/SaiStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>

#if defined(HAS_PERIPHERAL_SAI) && defined(HAL_SAI_MODULE_ENABLED)

namespace hal
{
    namespace
    {
        constexpr uint32_t minimumSampleRate = 8000;
        constexpr uint32_t maximumSampleRate = 192000;
        constexpr uint32_t fifoFillTimeoutInMilliseconds = 10;

        // The master clock is 256 times the sample rate. The older SAI divides the kernel clock by two on top of MCKDIV
        constexpr uint32_t masterClockRatio = 256;
#if defined(SAI_MCK_OUTPUT_ENABLE)
        constexpr uint32_t dividerFactor = 1;
#else
        constexpr uint32_t dividerFactor = 2;
#endif

        const std::array blockA{
            SAI1_Block_A,
#if defined(SAI2)
            SAI2_Block_A,
#endif
#if defined(SAI3)
            SAI3_Block_A,
#endif
        };

        const std::array blockB{
            SAI1_Block_B,
#if defined(SAI2)
            SAI2_Block_B,
#endif
#if defined(SAI3)
            SAI3_Block_B,
#endif
        };
    }

    SaiStm::SaiStm(uint8_t oneBasedIndex)
        : oneBasedIndex(oneBasedIndex)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= blockA.size());

        EnableClockSai(oneBasedIndex - 1);
    }

    SaiStm::~SaiStm()
    {
        ResetPeripheralSai(oneBasedIndex - 1);
        DisableClockSai(oneBasedIndex - 1);
    }

    uint8_t SaiStm::OneBasedIndex() const
    {
        return oneBasedIndex;
    }

    SaiBlockStm::SaiBlockStm(SaiStm& sai, Direction direction, const Config& config, GpioPinStm& sd, GpioPinStm& sck, GpioPinStm& fs, GpioPinStm& mclk)
        : oneBasedIndex(sai.OneBasedIndex())
        , direction(direction)
        , config(config)
        , sd(sd, PinConfigTypeStm::saiSd, sai.OneBasedIndex())
        , sck(sck, PinConfigTypeStm::saiSck, sai.OneBasedIndex())
        , fs(fs, PinConfigTypeStm::saiFs, sai.OneBasedIndex())
        , mclk(mclk, PinConfigTypeStm::saiMClock, sai.OneBasedIndex())
    {}

    bool SaiBlockStm::IsSupported(AudioFormat format)
    {
        return format.sampleRate >= minimumSampleRate && format.sampleRate <= maximumSampleRate && (format.channels == 1 || format.channels == 2);
    }

    uint32_t SaiBlockStm::ActualSampleRate() const
    {
        return actualSampleRate;
    }

    SAI_Block_TypeDef* SaiBlockStm::Block() const
    {
        return config.block == Config::Block::a ? blockA[oneBasedIndex - 1] : blockB[oneBasedIndex - 1];
    }

    volatile void* SaiBlockStm::DataRegister() const
    {
        return &Block()->DR;
    }

    void SaiBlockStm::Configure(AudioFormat format)
    {
        really_assert(IsSupported(format));

        const bool transmit = direction == Direction::transmit;
        const bool master = config.role == Config::Role::master;

        handle = {};
        handle.Instance = Block();
        handle.Init.AudioMode = master ? (transmit ? SAI_MODEMASTER_TX : SAI_MODEMASTER_RX) : (transmit ? SAI_MODESLAVE_TX : SAI_MODESLAVE_RX);
        handle.Init.Synchro = config.synchronization == Config::Synchronization::synchronousToOtherBlock ? SAI_SYNCHRONOUS : SAI_ASYNCHRONOUS;
        handle.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
        handle.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
        handle.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
        handle.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
        handle.Init.AudioFrequency = master ? format.sampleRate : SAI_AUDIO_FREQUENCY_MCKDIV;
        handle.Init.MonoStereoMode = format.channels == 1 ? SAI_MONOMODE : SAI_STEREOMODE;
        handle.Init.CompandingMode = SAI_NOCOMPANDING;
        handle.Init.TriState = SAI_OUTPUT_NOTRELEASED;
#if defined(SAI_MCK_OUTPUT_ENABLE)
        handle.Init.MckOutput = config.mclkOutput ? SAI_MCK_OUTPUT_ENABLE : SAI_MCK_OUTPUT_DISABLE;
        handle.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
#elif defined(SAI_CLKSOURCE_PLLSAI)
        handle.Init.ClockSource = config.kernelClock == Config::KernelClock::pllSai ? SAI_CLKSOURCE_PLLSAI : SAI_CLKSOURCE_PLLI2S;
#endif

        const HAL_StatusTypeDef status = HAL_SAI_InitProtocol(&handle, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2);
        really_assert(status == HAL_OK);

        if (!master)
        {
            actualSampleRate = format.sampleRate;
            return;
        }

        // The HAL stores the divider it derived from the requested rate; the rate it really gives follows from that
        const uint64_t divider = std::max<uint32_t>(handle.Init.Mckdiv, 1);
        actualSampleRate = static_cast<uint32_t>(KernelClockFrequency() / (masterClockRatio * dividerFactor * divider));

        const uint64_t error = actualSampleRate > format.sampleRate ? actualSampleRate - format.sampleRate : format.sampleRate - actualSampleRate;
        really_assert(error * 1000 <= static_cast<uint64_t>(format.sampleRate) * config.maxRateErrorPermille);
    }

    void SaiBlockStm::StartPeripheral()
    {
        handle.Instance->CR1 |= SAI_xCR1_DMAEN;

        if (direction == Direction::transmit)
        {
            // Starting with an empty FIFO would swap the left and right slots
            const uint32_t start = HAL_GetTick();

            while ((handle.Instance->SR & SAI_xSR_FLVL) == SAI_FIFOSTATUS_EMPTY)
                really_assert(HAL_GetTick() - start <= fifoFillTimeoutInMilliseconds);
        }

        __HAL_SAI_ENABLE(&handle);
    }

    void SaiBlockStm::StopPeripheral()
    {
        handle.Instance->CR1 &= ~SAI_xCR1_DMAEN;
        HAL_SAI_DeInit(&handle);
    }

    uint32_t SaiBlockStm::KernelClockFrequency()
    {
#if defined(STM32F4)
        return SAI_GetInputClock(&handle);
#else
        switch (oneBasedIndex)
        {
            case 1:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI1);
#if defined(SAI2)
            case 2:
#if defined(RCC_PERIPHCLK_SAI2)
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI2);
#else
                return HAL_RCCEx_GetPeriphCLKFreq(config.block == Config::Block::a ? RCC_PERIPHCLK_SAI2A : RCC_PERIPHCLK_SAI2B);
#endif
#endif
#if defined(SAI3)
            case 3:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI3);
#endif
            default:
                std::abort();
        }
#endif
    }
}

#endif
