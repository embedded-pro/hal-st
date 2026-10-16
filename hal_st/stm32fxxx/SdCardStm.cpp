#include "hal_st/stm32fxxx/SdCardStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>

#if defined(HAS_PERIPHERAL_SD)

namespace
{
#if defined(SDIO)
    constexpr uint32_t clockEdgeRising = SDIO_CLOCK_EDGE_RISING;
    constexpr uint32_t clockBypassDisabled = SDIO_CLOCK_BYPASS_DISABLE;
    constexpr uint32_t clockPowerSaveDisabled = SDIO_CLOCK_POWER_SAVE_DISABLE;
    constexpr uint32_t busWide1 = SDIO_BUS_WIDE_1B;
    constexpr uint32_t busWide4 = SDIO_BUS_WIDE_4B;
    constexpr uint32_t flowControlDisabled = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
#else
    constexpr uint32_t clockEdgeRising = SDMMC_CLOCK_EDGE_RISING;
#if defined(SD_EXTERNAL_DMA)
    constexpr uint32_t clockBypassDisabled = SDMMC_CLOCK_BYPASS_DISABLE;
#endif
    constexpr uint32_t clockPowerSaveDisabled = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    constexpr uint32_t busWide1 = SDMMC_BUS_WIDE_1B;
    constexpr uint32_t busWide4 = SDMMC_BUS_WIDE_4B;
    constexpr uint32_t flowControlDisabled = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
#endif

#if defined(SD_EXTERNAL_DMA)
    const std::array dmaStreams{ DMA2_Stream0, DMA2_Stream1, DMA2_Stream2, DMA2_Stream3, DMA2_Stream4, DMA2_Stream5, DMA2_Stream6, DMA2_Stream7 };
    const std::array dmaIrqs{ DMA2_Stream0_IRQn, DMA2_Stream1_IRQn, DMA2_Stream2_IRQn, DMA2_Stream3_IRQn, DMA2_Stream4_IRQn, DMA2_Stream5_IRQn, DMA2_Stream6_IRQn, DMA2_Stream7_IRQn };

    // Only some F7 devices, with a DMA_CHANNEL_15, have the channels above 7
#if defined(DMA_CHANNEL_15)
    constexpr uint8_t maxDmaChannel = 15;
#else
    constexpr uint8_t maxDmaChannel = 7;
#endif

    uint32_t DmaChannelSelection(uint8_t channel)
    {
        return uint32_t{ channel } << DMA_SxCR_CHSEL_Pos;
    }
#endif

    // F4 numbers its single SDIO instance 0 in the pin tables, the SDMMC instances are numbered from 1
    uint8_t PinPeripheralIndex([[maybe_unused]] uint8_t oneBasedIndex)
    {
#if defined(SDIO)
        return 0;
#else
        return oneBasedIndex;
#endif
    }

    uint32_t AllocationUnitKiB(uint8_t code)
    {
        static constexpr std::array<uint32_t, 16> kibByCode{ { 0, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 12288, 16384, 24576, 32768, 65536 } };

        return kibByCode[code & 0x0f];
    }
}

namespace hal
{
    SdCardStm::SdCardStm(uint8_t oneBasedIndex, GpioPinStm& clock, GpioPinStm& command, GpioPinStm& data0, GpioPinStm& data1, GpioPinStm& data2, GpioPinStm& data3, const Config& config, const DirectionPins& directionPins)
        : SdmmcBlockDeviceStm<detail::SdCardHal>{ config }
        , clock{ clock, PinConfigTypeStm::sdClk, PinPeripheralIndex(oneBasedIndex) }
        , command{ command, PinConfigTypeStm::sdCmd, PinPeripheralIndex(oneBasedIndex) }
        , data0{ data0, PinConfigTypeStm::sdD0, PinPeripheralIndex(oneBasedIndex) }
        , data1{ data1, PinConfigTypeStm::sdD1, PinPeripheralIndex(oneBasedIndex) }
        , data2{ data2, PinConfigTypeStm::sdD2, PinPeripheralIndex(oneBasedIndex) }
        , data3{ data3, PinConfigTypeStm::sdD3, PinPeripheralIndex(oneBasedIndex) }
        , data0Direction{ directionPins.data0, PinConfigTypeStm::sdD0Dir, PinPeripheralIndex(oneBasedIndex) }
        , data123Direction{ directionPins.data123, PinConfigTypeStm::sdD123Dir, PinPeripheralIndex(oneBasedIndex) }
        , commandDirection{ directionPins.command, PinConfigTypeStm::sdCDir, PinPeripheralIndex(oneBasedIndex) }
        , oneBasedIndex{ oneBasedIndex }
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralSd.size());
        really_assert(config.busWidth == BusWidth::oneBit || (&data1 != &dummyPinStm && &data2 != &dummyPinStm && &data3 != &dummyPinStm));

        EnableClockSd(oneBasedIndex - 1);
#if defined(SD_EXTERNAL_DMA)
        really_assert(config.dma.dma == 1 && config.dma.stream < dmaStreams.size() && config.dma.channel <= maxDmaChannel);
        __HAL_RCC_DMA2_CLK_ENABLE();
#endif
        InitSdmmc(config);
    }

    SdCardStm::~SdCardStm()
    {
#if defined(SD_EXTERNAL_DMA)
        dmaInterrupt.reset();
#endif
        Deactivate();
        HAL_SD_DeInit(&Handle());
#if defined(SD_EXTERNAL_DMA)
        if (dmaInitialized)
            HAL_DMA_DeInit(&dma);
#endif
        DisableClockSd(oneBasedIndex - 1);
    }

    uint32_t SdCardStm::AllocationUnitBlocks() const
    {
        return allocationUnitBlocks;
    }

    const SdCardStm::Identification& SdCardStm::CardIdentification() const
    {
        return identification;
    }

    void SdCardStm::InitSdmmc(const Config& config)
    {
        auto& sd = Handle();
        auto& init = sd.Init;

        sd.Instance = peripheralSd[oneBasedIndex - 1];
        init.ClockEdge = clockEdgeRising;
#if defined(SD_EXTERNAL_DMA)
        init.ClockBypass = clockBypassDisabled;
#endif
        init.ClockPowerSave = clockPowerSaveDisabled;
        init.HardwareFlowControl = flowControlDisabled;
        init.ClockDiv = config.clockDivider;
#if defined(SD_EXTERNAL_DMA)
        init.BusWide = busWide1;
#else
        init.BusWide = config.busWidth == BusWidth::fourBit ? busWide4 : busWide1;
#endif

        if (HAL_SD_Init(&sd) != HAL_OK)
            return;

#if defined(SD_EXTERNAL_DMA)
        if (config.busWidth == BusWidth::fourBit && HAL_SD_ConfigWideBusOperation(&sd, busWide4) != HAL_OK)
            return;
#endif

        HAL_SD_CardInfoTypeDef info{};

        if (HAL_SD_GetCardInfo(&sd, &info) != HAL_OK)
            return;

        really_assert(info.LogBlockSize == blockSize);

        ReadCardRegisters();
#if defined(SD_EXTERNAL_DMA)
        InitDma(config);
#endif
        Activate(info.LogBlockNbr, peripheralSdIrq[oneBasedIndex - 1]);
    }

    void SdCardStm::ReadCardRegisters()
    {
        auto& sd = Handle();

        HAL_SD_CardCIDTypeDef cid{};
        if (HAL_SD_GetCardCID(&sd, &cid) == HAL_OK)
            identification = { cid.ManufacturerID, cid.OEM_AppliID,
                { { static_cast<char>(cid.ProdName1 >> 24), static_cast<char>(cid.ProdName1 >> 16), static_cast<char>(cid.ProdName1 >> 8), static_cast<char>(cid.ProdName1), static_cast<char>(cid.ProdName2) } },
                cid.ProdRev, cid.ProdSN, cid.ManufactDate };

        HAL_SD_CardStatusTypeDef status{};
        if (HAL_SD_GetCardStatus(&sd, &status) == HAL_OK)
            allocationUnitBlocks = AllocationUnitKiB(status.AllocationUnitSize) * 1024 / blockSize;
    }

#if defined(SD_EXTERNAL_DMA)
    void SdCardStm::InitDma(const Config& config)
    {
        dma.Instance = dmaStreams[config.dma.stream];
        dma.Init.Channel = DmaChannelSelection(config.dma.channel);
        dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
        dma.Init.PeriphInc = DMA_PINC_DISABLE;
        dma.Init.MemInc = DMA_MINC_ENABLE;
        dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
        dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
        dma.Init.Mode = DMA_PFCTRL;
        dma.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        dma.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
        dma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
        dma.Init.MemBurst = DMA_MBURST_INC4;
        dma.Init.PeriphBurst = DMA_PBURST_INC4;

        auto status = HAL_DMA_Init(&dma);
        really_assert(status == HAL_OK);
        dmaInitialized = true;

        __HAL_LINKDMA(&Handle(), hdmarx, dma);
        __HAL_LINKDMA(&Handle(), hdmatx, dma);

        dmaInterrupt.emplace(dmaIrqs[config.dma.stream], config.priority, [this]
            {
                HAL_DMA_IRQHandler(&dma);
            });
    }
#endif
}

#endif
