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

        // The master clock is 256 times the sample rate. The older SAI divides the kernel clock by two on top of MCKDIV, and a MCKDIV of 0 bypasses the divider
        constexpr uint32_t masterClockRatio = 256;
#if defined(SAI_MCK_OUTPUT_ENABLE)
        constexpr uint32_t dividerFactor = 1;
#else
        constexpr uint32_t dividerFactor = 2;
#endif

        constexpr uint32_t maxMasterClockDivider = SAI_xCR1_MCKDIV >> SAI_xCR1_MCKDIV_Pos;

        uint64_t TotalDivision(uint32_t masterClockDivider)
        {
            return masterClockDivider == 0 ? 1 : static_cast<uint64_t>(masterClockDivider) * dividerFactor;
        }

        // The HAL only accepts the standard rates when it derives the divider itself, so the divider that gives the rate closest to the requested one is chosen here
        uint32_t MasterClockDivider(uint64_t kernelClock, uint32_t frameRate)
        {
            const uint64_t target = static_cast<uint64_t>(frameRate) * masterClockRatio;
            const uint32_t lower = static_cast<uint32_t>(std::min<uint64_t>(kernelClock / (target * dividerFactor), maxMasterClockDivider + 1));
            const uint32_t upper = lower + 1;

            const auto scaledError = [&](uint32_t divider)
            {
                const uint64_t division = TotalDivision(divider);
                const uint64_t rate = division * target;
                return (rate > kernelClock ? rate - kernelClock : kernelClock - rate);
            };

            // Relative error of a candidate is scaledError / division, compared without dividing
            const uint32_t best = scaledError(upper) * TotalDivision(lower) < scaledError(lower) * TotalDivision(upper) ? upper : lower;
            really_assert(best <= maxMasterClockDivider);

            return best;
        }

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
    {
        really_assert(config.synchronization == Config::Synchronization::asynchronous || config.role == Config::Role::slave);
    }

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

    SAI_Block_TypeDef* SaiBlockStm::OtherBlock() const
    {
        return config.block == Config::Block::a ? blockB[oneBasedIndex - 1] : blockA[oneBasedIndex - 1];
    }

    volatile void* SaiBlockStm::DataRegister() const
    {
        return &Block()->DR;
    }

    bool SaiBlockStm::Pdm() const
    {
        return config.mode == Config::Mode::pdm;
    }

    void SaiBlockStm::Configure(AudioFormat format)
    {
        const bool transmit = direction == Direction::transmit;
        const bool master = config.role == Config::Role::master;

        // A PDM stream is read as two 16-bit slots per frame, each holding 16 clock cycles of one microphone, so a frame carries two words
        if (Pdm())
            really_assert(!transmit && format.channels == 1 && format.sampleRate % 2 == 0 && IsSupported({ format.sampleRate / 2, 2 }));
        else
            really_assert(IsSupported(format));

        const uint32_t frameRate = Pdm() ? format.sampleRate / 2 : format.sampleRate;

        if (master && config.audioClock != nullptr)
            config.audioClock->Select(frameRate);

        handle = {};
        handle.Instance = Block();
        handle.Init.AudioMode = master ? (transmit ? SAI_MODEMASTER_TX : SAI_MODEMASTER_RX) : (transmit ? SAI_MODESLAVE_TX : SAI_MODESLAVE_RX);
        handle.Init.Synchro = config.synchronization == Config::Synchronization::synchronousToOtherBlock ? SAI_SYNCHRONOUS : SAI_ASYNCHRONOUS;
        handle.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
        handle.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
        handle.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
        handle.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
        handle.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
        handle.Init.MonoStereoMode = format.channels == 1 && !Pdm() ? SAI_MONOMODE : SAI_STEREOMODE;
        handle.Init.CompandingMode = SAI_NOCOMPANDING;
        handle.Init.TriState = SAI_OUTPUT_NOTRELEASED;
#if defined(SAI_MCK_OUTPUT_ENABLE)
        handle.Init.MckOutput = config.mclkOutput ? SAI_MCK_OUTPUT_ENABLE : SAI_MCK_OUTPUT_DISABLE;
        handle.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
#elif defined(SAI_CLKSOURCE_PLLSAI)
        handle.Init.ClockSource = config.kernelClock == Config::KernelClock::pllSai ? SAI_CLKSOURCE_PLLSAI : SAI_CLKSOURCE_PLLI2S;
#endif

        uint64_t kernelClock = 0;

        if (master)
        {
            kernelClock = KernelClockFrequency();
            handle.Init.Mckdiv = MasterClockDivider(kernelClock, frameRate);
        }

        const HAL_StatusTypeDef status = HAL_SAI_InitProtocol(&handle, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2);
        really_assert(status == HAL_OK);

        if (Pdm() && config.pdmSampleEdge == Config::SampleEdge::falling)
            handle.Instance->CR1 &= ~SAI_xCR1_CKSTR;

        if (!master)
        {
            actualSampleRate = format.sampleRate;
            return;
        }

        const uint32_t actualFrameRate = static_cast<uint32_t>(kernelClock / (masterClockRatio * TotalDivision(handle.Init.Mckdiv)));
        actualSampleRate = Pdm() ? actualFrameRate * 2 : actualFrameRate;

        const uint64_t error = actualFrameRate > frameRate ? actualFrameRate - frameRate : frameRate - actualFrameRate;
        really_assert(error * 1000 <= static_cast<uint64_t>(frameRate) * config.maxRateErrorPermille);
    }

    void SaiBlockStm::StartPeripheral()
    {
        // The block that provides the clocks must be enabled after the blocks that follow it
        if (config.synchronization == Config::Synchronization::synchronousToOtherBlock)
            really_assert((OtherBlock()->CR1 & SAI_xCR1_SAIEN) == 0);

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
        // A block that follows the clocks of the other one cannot finish its frame once they are gone
        if (config.synchronization == Config::Synchronization::asynchronous)
        {
            const uint32_t other = OtherBlock()->CR1;
            really_assert(!((other & SAI_xCR1_SAIEN) != 0 && (other & SAI_xCR1_SYNCEN) == SAI_xCR1_SYNCEN_0));
        }

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
