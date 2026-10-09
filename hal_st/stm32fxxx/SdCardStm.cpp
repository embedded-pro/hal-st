#include "hal_st/stm32fxxx/SdCardStm.hpp"
#include "hal_st/stm32fxxx/DataCacheStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>
#include <utility>

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

    constexpr uint32_t wordSize = 4;

    // The HAL programs the DMA with 16-bit word counts on F4 and F7, and the SDMMC data length is 25 bits on H5 and H7
#if defined(SD_EXTERNAL_DMA)
    constexpr uint32_t maxBlocksPerChunk = 511;
#else
    constexpr uint32_t maxBlocksPerChunk = 65535;
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

    hal::BlockDevice::Result MapError(uint32_t errorCode)
    {
        if ((errorCode & (HAL_SD_ERROR_CMD_CRC_FAIL | HAL_SD_ERROR_DATA_CRC_FAIL)) != 0)
            return hal::BlockDevice::Result::crcError;

        if ((errorCode & (HAL_SD_ERROR_CMD_RSP_TIMEOUT | HAL_SD_ERROR_DATA_TIMEOUT | HAL_SD_ERROR_TIMEOUT)) != 0)
            return hal::BlockDevice::Result::timeout;

        if ((errorCode & (HAL_SD_ERROR_ADDR_OUT_OF_RANGE | HAL_SD_ERROR_ADDR_MISALIGNED)) != 0)
            return hal::BlockDevice::Result::outOfRange;

        if ((errorCode & (HAL_SD_ERROR_WRITE_PROT_VIOLATION | HAL_SD_ERROR_LOCK_UNLOCK_FAILED | HAL_SD_ERROR_WP_ERASE_SKIP)) != 0)
            return hal::BlockDevice::Result::writeProtected;

        return hal::BlockDevice::Result::failed;
    }
}

namespace hal
{
    SdCardStm::SdCardStm(uint8_t oneBasedIndex, GpioPinStm& clock, GpioPinStm& command, GpioPinStm& data0, GpioPinStm& data1, GpioPinStm& data2, GpioPinStm& data3, const Config& config, const DirectionPins& directionPins)
        : clock{ clock, PinConfigTypeStm::sdClk, PinPeripheralIndex(oneBasedIndex) }
        , command{ command, PinConfigTypeStm::sdCmd, PinPeripheralIndex(oneBasedIndex) }
        , data0{ data0, PinConfigTypeStm::sdD0, PinPeripheralIndex(oneBasedIndex) }
        , data1{ data1, PinConfigTypeStm::sdD1, PinPeripheralIndex(oneBasedIndex) }
        , data2{ data2, PinConfigTypeStm::sdD2, PinPeripheralIndex(oneBasedIndex) }
        , data3{ data3, PinConfigTypeStm::sdD3, PinPeripheralIndex(oneBasedIndex) }
        , data0Direction{ directionPins.data0, PinConfigTypeStm::sdD0Dir, PinPeripheralIndex(oneBasedIndex) }
        , data123Direction{ directionPins.data123, PinConfigTypeStm::sdD123Dir, PinPeripheralIndex(oneBasedIndex) }
        , commandDirection{ directionPins.command, PinConfigTypeStm::sdCDir, PinPeripheralIndex(oneBasedIndex) }
        , oneBasedIndex{ oneBasedIndex }
        , config{ config }
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralSd.size());
        really_assert(config.busyPollInterval.count() > 0);
        really_assert(config.busWidth == BusWidth::oneBit || (&data1 != &dummyPinStm && &data2 != &dummyPinStm && &data3 != &dummyPinStm));

        handle.owner = this;
        EnableClockSd(oneBasedIndex - 1);
#if defined(SD_EXTERNAL_DMA)
        really_assert(config.dma.dma == 1 && config.dma.stream < dmaStreams.size() && config.dma.channel <= maxDmaChannel);
        __HAL_RCC_DMA2_CLK_ENABLE();
#endif
        InitSdmmc();
    }

    SdCardStm::~SdCardStm()
    {
#if defined(SD_EXTERNAL_DMA)
        dmaInterrupt.reset();
#endif
        sdInterrupt.reset();
        timer.Cancel();

        if (phase == Phase::transferring)
            HAL_SD_Abort(&handle);

        HAL_SD_DeInit(&handle);
#if defined(SD_EXTERNAL_DMA)
        if (dmaInitialized)
            HAL_DMA_DeInit(&dma);
#endif
        DisableClockSd(oneBasedIndex - 1);
    }

    uint32_t SdCardStm::BlockSize() const
    {
        return blockSize;
    }

    uint32_t SdCardStm::NumberOfBlocks() const
    {
        return numberOfBlocks;
    }

    void SdCardStm::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        Begin(Operation::read, onDone);
        AssertBuffer(buffer.begin(), buffer.size());

        auto blockCount = static_cast<uint32_t>(buffer.size() / blockSize);

        if (!cardPresent)
            Complete(Result::notPresent);
        else if (blockCount == 0)
            Complete(Result::success);
        else if (!IsValidRange(firstBlock, blockCount))
            Complete(Result::outOfRange);
        else
        {
            readBuffer = buffer;
            CleanDataCache(buffer.begin(), buffer.size());
            StartTransfer(buffer.begin(), firstBlock, blockCount);
        }
    }

    void SdCardStm::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        Begin(Operation::write, onDone);
        AssertBuffer(buffer.begin(), buffer.size());

        auto blockCount = static_cast<uint32_t>(buffer.size() / blockSize);

        if (!cardPresent)
            Complete(Result::notPresent);
        else if (blockCount == 0)
            Complete(Result::success);
        else if (!IsValidRange(firstBlock, blockCount))
            Complete(Result::outOfRange);
        else
        {
            CleanDataCache(buffer.begin(), buffer.size());
            StartTransfer(const_cast<uint8_t*>(buffer.begin()), firstBlock, blockCount);
        }
    }

    void SdCardStm::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        Begin(Operation::erase, onDone);

        if (!cardPresent)
            Complete(Result::notPresent);
        else if (beginBlock > endBlock || endBlock > numberOfBlocks)
            Complete(Result::outOfRange);
        else if (beginBlock == endBlock)
            Complete(Result::success);
        else if (HAL_SD_Erase(&handle, beginBlock, endBlock - 1) != HAL_OK)
            Complete(MapError(HAL_SD_GetError(&handle)));
        else
            WaitForCard();
    }

    void SdCardStm::OnReceiveComplete(SD_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->PostEvent(Event::transferred, HAL_SD_ERROR_NONE);
    }

    void SdCardStm::OnTransmitComplete(SD_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->PostEvent(Event::transferred, HAL_SD_ERROR_NONE);
    }

    void SdCardStm::OnError(SD_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->PostEvent(Event::failed, HAL_SD_GetError(handle));
    }

    void SdCardStm::OnAbort(SD_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->PostEvent(Event::failed, HAL_SD_GetError(handle));
    }

    void SdCardStm::InitSdmmc()
    {
        auto& init = handle.Init;

        handle.Instance = peripheralSd[oneBasedIndex - 1];
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

        if (HAL_SD_Init(&handle) != HAL_OK)
            return;

#if defined(SD_EXTERNAL_DMA)
        if (config.busWidth == BusWidth::fourBit && HAL_SD_ConfigWideBusOperation(&handle, busWide4) != HAL_OK)
            return;
#endif

        HAL_SD_CardInfoTypeDef info{};

        if (HAL_SD_GetCardInfo(&handle, &info) != HAL_OK)
            return;

        really_assert(info.LogBlockSize == blockSize);
        numberOfBlocks = info.LogBlockNbr;
        handle.ErrorCode = HAL_SD_ERROR_NONE;
        cardPresent = true;

        RegisterCallbacks();
#if defined(SD_EXTERNAL_DMA)
        InitDma();
#endif
        RegisterInterrupts();
    }

    void SdCardStm::RegisterCallbacks()
    {
        auto transmitStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_TX_CPLT_CB_ID, &SdCardStm::OnTransmitComplete);
        auto receiveStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_RX_CPLT_CB_ID, &SdCardStm::OnReceiveComplete);
        auto errorStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_ERROR_CB_ID, &SdCardStm::OnError);
        auto abortStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_ABORT_CB_ID, &SdCardStm::OnAbort);
        really_assert(transmitStatus == HAL_OK && receiveStatus == HAL_OK && errorStatus == HAL_OK && abortStatus == HAL_OK);
    }

    void SdCardStm::RegisterInterrupts()
    {
        sdInterrupt.emplace(peripheralSdIrq[oneBasedIndex - 1], config.priority, [this]
            {
                HAL_SD_IRQHandler(&handle);
            });
#if defined(SD_EXTERNAL_DMA)
        dmaInterrupt.emplace(dmaIrqs[config.dma.stream], config.priority, [this]
            {
                HAL_DMA_IRQHandler(&dma);
            });
#endif
    }

#if defined(SD_EXTERNAL_DMA)
    void SdCardStm::InitDma()
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

        __HAL_LINKDMA(&handle, hdmarx, dma);
        __HAL_LINKDMA(&handle, hdmatx, dma);
    }
#endif

    void SdCardStm::Begin(Operation newOperation, const infra::Function<void(Result)>& newOnDone)
    {
        really_assert(!onDone);
        onDone = newOnDone;
        ++operationId;
        operation = newOperation;
        phase = Phase::idle;
        readBuffer = infra::ByteRange();
    }

    void SdCardStm::AssertBuffer(const uint8_t* address, std::size_t size) const
    {
        auto location = reinterpret_cast<uintptr_t>(address);

        really_assert(size % blockSize == 0);
        really_assert(location % wordSize == 0);
        really_assert(!DataCacheEnabled() || (location % dataCacheLineSize == 0 && size % dataCacheLineSize == 0));
    }

    bool SdCardStm::IsValidRange(uint32_t firstBlock, uint32_t blockCount) const
    {
        return firstBlock <= numberOfBlocks && blockCount <= numberOfBlocks - firstBlock;
    }

    void SdCardStm::StartTransfer(uint8_t* buffer, uint32_t firstBlock, uint32_t blockCount)
    {
        cursor = buffer;
        nextBlock = firstBlock;
        blocksLeft = blockCount;
        StartChunk();
    }

    void SdCardStm::StartChunk()
    {
        chunkBlocks = std::min(blocksLeft, maxBlocksPerChunk);

        auto status = operation == Operation::read
                          ? HAL_SD_ReadBlocks_DMA(&handle, cursor, nextBlock, chunkBlocks)
                          : HAL_SD_WriteBlocks_DMA(&handle, cursor, nextBlock, chunkBlocks);

        if (status != HAL_OK)
        {
            Complete(MapError(HAL_SD_GetError(&handle)));
            return;
        }

        phase = Phase::transferring;
        timer.Start(config.transferTimeout, [this]
            {
                OnTransferTimeout();
            });
    }

    void SdCardStm::ChunkTransferred()
    {
        cursor += chunkBlocks * blockSize;
        nextBlock += chunkBlocks;
        blocksLeft -= chunkBlocks;

        if (operation == Operation::write)
            WaitForCard();
        else
            ContinueOrComplete();
    }

    void SdCardStm::ContinueOrComplete()
    {
        if (blocksLeft == 0)
            Complete(Result::success);
        else
            StartChunk();
    }

    void SdCardStm::WaitForCard()
    {
        phase = Phase::waitingForCard;
        remainingPolls = static_cast<uint32_t>(config.busyTimeout / config.busyPollInterval);
        PollCard();
    }

    void SdCardStm::PollCard()
    {
        handle.ErrorCode = HAL_SD_ERROR_NONE;
        auto state = HAL_SD_GetCardState(&handle);

        if (handle.ErrorCode != HAL_SD_ERROR_NONE)
            Complete(MapError(handle.ErrorCode));
        else if (state == HAL_SD_CARD_TRANSFER && operation == Operation::write)
            ContinueOrComplete();
        else if (state == HAL_SD_CARD_TRANSFER)
            Complete(Result::success);
        else if (state == HAL_SD_CARD_ERROR)
            Complete(Result::failed);
        else if (remainingPolls == 0)
            Complete(Result::timeout);
        else
        {
            --remainingPolls;
            timer.Start(config.busyPollInterval, [this]
                {
                    PollCard();
                });
        }
    }

    void SdCardStm::OnTransferTimeout()
    {
        HAL_SD_Abort(&handle);
        Complete(Result::timeout);
    }

    void SdCardStm::PostEvent(Event event, uint32_t errorCode)
    {
        // The HAL reports a failed stop command and then still calls the completion callback
        if (pendingEvent != Event::failed)
        {
            pendingEvent = event;
            pendingErrorCode = errorCode;
            pendingEventOperation = operationId;
        }

        if (!eventScheduled)
        {
            eventScheduled = true;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    HandleEvent();
                });
        }
    }

    void SdCardStm::HandleEvent()
    {
        eventScheduled = false;
        auto event = std::exchange(pendingEvent, Event::none);

        if (phase != Phase::transferring || event == Event::none || pendingEventOperation != operationId)
            return;

        timer.Cancel();

        if (event == Event::failed)
            Complete(MapError(pendingErrorCode));
        else
            ChunkTransferred();
    }

    void SdCardStm::Complete(Result result)
    {
        if (phase == Phase::completing)
            return;

        timer.Cancel();
        phase = Phase::completing;
        completionResult = result;
        infra::EventDispatcher::Instance().Schedule([this]
            {
                Finish();
            });
    }

    void SdCardStm::Finish()
    {
        if (operation == Operation::read)
            InvalidateDataCache(readBuffer.begin(), readBuffer.size());

        operation = Operation::none;
        phase = Phase::idle;
        onDone(completionResult);
    }
}

#endif
