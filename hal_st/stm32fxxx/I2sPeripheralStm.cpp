#include "hal_st/stm32fxxx/I2sPeripheralStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <cstdlib>

#if defined(HAS_PERIPHERAL_SPI) && defined(HAL_I2S_MODULE_ENABLED)

namespace hal
{
    namespace
    {
        constexpr uint32_t minimumSampleRate = 8000;
        constexpr uint32_t maximumSampleRate = 192000;
        constexpr uint32_t drainTimeoutInMilliseconds = 10;

        // Both frame formats are 16-bit data in a 16-bit slot, two slots per frame
        constexpr uint32_t bitClockRatio = 32;
        constexpr uint32_t masterClockRatio = 256;
    }

    I2sPeripheralStm::I2sPeripheralStm(uint8_t oneBasedIndex, Direction direction, const Config& config, GpioPinStm& sd, GpioPinStm& ck, GpioPinStm& ws, GpioPinStm& mclk)
        : oneBasedIndex(oneBasedIndex)
        , direction(direction)
        , config(config)
        , sd(sd, PinConfigTypeStm::i2sSd, oneBasedIndex)
        , ck(ck, PinConfigTypeStm::i2sCk, oneBasedIndex)
        , ws(ws, PinConfigTypeStm::i2sWs, oneBasedIndex)
        , mclk(mclk, PinConfigTypeStm::i2sMClock, oneBasedIndex)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralSpi.size());
        really_assert(IS_I2S_ALL_INSTANCE(peripheralSpi[oneBasedIndex - 1]));

        EnableClockSpi(oneBasedIndex - 1);
    }

    I2sPeripheralStm::~I2sPeripheralStm()
    {
        ResetPeripheralSpi(oneBasedIndex - 1);
        DisableClockSpi(oneBasedIndex - 1);
    }

    bool I2sPeripheralStm::IsSupported(AudioFormat format)
    {
        return format.sampleRate >= minimumSampleRate && format.sampleRate <= maximumSampleRate && (format.channels == 1 || format.channels == 2);
    }

    uint32_t I2sPeripheralStm::ActualSampleRate() const
    {
        return actualSampleRate;
    }

    volatile void* I2sPeripheralStm::DataRegister() const
    {
#if defined(SPI_CR2_TXDMAEN)
        return &peripheralSpi[oneBasedIndex - 1]->DR;
#else
        return direction == Direction::transmit ? static_cast<volatile void*>(&peripheralSpi[oneBasedIndex - 1]->TXDR) : static_cast<volatile void*>(&peripheralSpi[oneBasedIndex - 1]->RXDR);
#endif
    }

    bool I2sPeripheralStm::Pdm() const
    {
        return config.mode == Config::Mode::pdm;
    }

    void I2sPeripheralStm::Configure(AudioFormat format)
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
        handle.Instance = peripheralSpi[oneBasedIndex - 1];
        handle.Init.Mode = master ? (transmit ? I2S_MODE_MASTER_TX : I2S_MODE_MASTER_RX) : (transmit ? I2S_MODE_SLAVE_TX : I2S_MODE_SLAVE_RX);
        handle.Init.Standard = I2S_STANDARD_PHILIPS;
        handle.Init.DataFormat = I2S_DATAFORMAT_16B;
        handle.Init.MCLKOutput = config.mclkOutput ? I2S_MCLKOUTPUT_ENABLE : I2S_MCLKOUTPUT_DISABLE;
        handle.Init.AudioFreq = frameRate;
        handle.Init.CPOL = Pdm() && config.pdmSampleEdge == Config::SampleEdge::falling ? I2S_CPOL_HIGH : I2S_CPOL_LOW;
#if defined(I2S_CLOCK_PLL)
        handle.Init.ClockSource = config.kernelClock == Config::KernelClock::external ? I2S_CLOCK_EXTERNAL : I2S_CLOCK_PLL;
#endif
#if defined(I2S_FULLDUPLEXMODE_DISABLE)
        handle.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
#endif
#if defined(I2S_WS_INVERSION_DISABLE)
        handle.Init.FirstBit = I2S_FIRSTBIT_MSB;
        handle.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
        handle.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
        handle.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_DISABLE;
#endif

        const HAL_StatusTypeDef status = HAL_I2S_Init(&handle);
        really_assert(status == HAL_OK);

        if (!master)
        {
            actualSampleRate = format.sampleRate;
            return;
        }

        // The HAL leaves the divider it derived from the requested rate in the registers; the rate it really gives follows from that
#if defined(SPI_I2SPR_I2SDIV)
        const uint32_t prescaler = handle.Instance->I2SPR;
        const uint32_t divider = 2 * (prescaler & SPI_I2SPR_I2SDIV) + ((prescaler & SPI_I2SPR_ODD) >> SPI_I2SPR_ODD_Pos);
#else
        const uint32_t configuration = handle.Instance->I2SCFGR;
        const uint32_t divider = 2 * ((configuration & SPI_I2SCFGR_I2SDIV) >> SPI_I2SCFGR_I2SDIV_Pos) + ((configuration & SPI_I2SCFGR_ODD) >> SPI_I2SCFGR_ODD_Pos);
#endif
        const uint64_t ratio = (config.mclkOutput ? masterClockRatio : bitClockRatio) * static_cast<uint64_t>(std::max<uint32_t>(divider, 1));
        const uint32_t actualFrameRate = static_cast<uint32_t>(KernelClockFrequency() / ratio);
        actualSampleRate = Pdm() ? actualFrameRate * 2 : actualFrameRate;

        const uint64_t error = actualFrameRate > frameRate ? actualFrameRate - frameRate : frameRate - actualFrameRate;
        really_assert(error * 1000 <= static_cast<uint64_t>(frameRate) * config.maxRateErrorPermille);
    }

    void I2sPeripheralStm::StartPeripheral()
    {
        SetDmaRequest(true);

#if defined(SPI_CR2_TXDMAEN)
        if (direction == Direction::transmit)
        {
            // Starting with an empty data register would swap the left and right slots
            const uint32_t start = HAL_GetTick();

            while ((handle.Instance->SR & SPI_SR_TXE) != 0)
                really_assert(HAL_GetTick() - start <= drainTimeoutInMilliseconds);
        }

        __HAL_I2S_ENABLE(&handle);
#else
        __HAL_I2S_ENABLE(&handle);
        SET_BIT(handle.Instance->CR1, SPI_CR1_CSTART);
#endif
    }

    void I2sPeripheralStm::StopPeripheral()
    {
        SetDmaRequest(false);

#if defined(SPI_CR2_TXDMAEN)
        if (direction == Direction::transmit)
        {
            // Let the sample in flight leave before the clocks stop; a timeout only costs a short glitch
            const uint32_t start = HAL_GetTick();

            while (((handle.Instance->SR & SPI_SR_TXE) == 0 || (handle.Instance->SR & SPI_SR_BSY) != 0) && HAL_GetTick() - start <= drainTimeoutInMilliseconds)
                ;
        }
#endif

        HAL_I2S_DeInit(&handle);
    }

    void I2sPeripheralStm::SetDmaRequest(bool enabled)
    {
        const bool transmit = direction == Direction::transmit;

#if defined(SPI_CR2_TXDMAEN)
        const uint32_t mask = transmit ? SPI_CR2_TXDMAEN : SPI_CR2_RXDMAEN;

        if (enabled)
            handle.Instance->CR2 |= mask;
        else
            handle.Instance->CR2 &= ~mask;
#else
        const uint32_t mask = transmit ? SPI_CFG1_TXDMAEN : SPI_CFG1_RXDMAEN;

        if (enabled)
            handle.Instance->CFG1 |= mask;
        else
            handle.Instance->CFG1 &= ~mask;
#endif
    }

    uint32_t I2sPeripheralStm::KernelClockFrequency() const
    {
#if defined(STM32F7)
        if (config.kernelClock == Config::KernelClock::external)
            return EXTERNAL_CLOCK_VALUE;

        // The HAL keeps this calculation private
        const uint32_t pllInput = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) == RCC_PLLSOURCE_HSI ? HSI_VALUE : HSE_VALUE;
        const uint32_t vcoInput = pllInput / (RCC->PLLCFGR & RCC_PLLCFGR_PLLM);
        const uint32_t vcoMultiplier = (RCC->PLLI2SCFGR & RCC_PLLI2SCFGR_PLLI2SN) >> RCC_PLLI2SCFGR_PLLI2SN_Pos;
        const uint32_t outputDivider = (RCC->PLLI2SCFGR & RCC_PLLI2SCFGR_PLLI2SR) >> RCC_PLLI2SCFGR_PLLI2SR_Pos;

        return vcoInput * vcoMultiplier / outputDivider;
#elif defined(STM32F4) || defined(STM32G4)
        return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2S);
#elif defined(STM32H5)
        switch (oneBasedIndex)
        {
            case 1:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
            case 2:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI2);
            case 3:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI3);
            default:
                std::abort();
        }
#elif defined(STM32H7)
        return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI123);
#else
#error "I2S kernel clock is not known for this family"
#endif
    }
}

#endif
