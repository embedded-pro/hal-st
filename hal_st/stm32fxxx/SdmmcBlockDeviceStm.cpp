#include "hal_st/stm32fxxx/SdmmcBlockDeviceStm.hpp"
#include "hal_st/stm32fxxx/DataCacheStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <utility>

#if defined(HAS_PERIPHERAL_SD)

namespace
{
    constexpr uint32_t wordSize = 4;
#if !defined(SD_EXTERNAL_DMA)
    constexpr uint32_t switchErrorStatus = 1u << 7;
    constexpr uint8_t trimSupported = 1u << 4;
#endif

    hal::BlockDevice::Result MapError(uint32_t errorCode)
    {
        if ((errorCode & (SDMMC_ERROR_CMD_CRC_FAIL | SDMMC_ERROR_DATA_CRC_FAIL)) != 0)
            return hal::BlockDevice::Result::crcError;

        if ((errorCode & (SDMMC_ERROR_CMD_RSP_TIMEOUT | SDMMC_ERROR_DATA_TIMEOUT | SDMMC_ERROR_TIMEOUT)) != 0)
            return hal::BlockDevice::Result::timeout;

        if ((errorCode & (SDMMC_ERROR_ADDR_OUT_OF_RANGE | SDMMC_ERROR_ADDR_MISALIGNED)) != 0)
            return hal::BlockDevice::Result::outOfRange;

        if ((errorCode & (SDMMC_ERROR_WRITE_PROT_VIOLATION | SDMMC_ERROR_LOCK_UNLOCK_FAILED | SDMMC_ERROR_WP_ERASE_SKIP)) != 0)
            return hal::BlockDevice::Result::writeProtected;

        return hal::BlockDevice::Result::failed;
    }
}

namespace hal
{
    namespace detail
    {
        HAL_StatusTypeDef SdCardHal::ReadBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount)
        {
            return HAL_SD_ReadBlocks_DMA(&handle, data, firstBlock, blockCount);
        }

        HAL_StatusTypeDef SdCardHal::WriteBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount)
        {
            return HAL_SD_WriteBlocks_DMA(&handle, data, firstBlock, blockCount);
        }

        HAL_StatusTypeDef SdCardHal::Erase(Handle& handle, uint32_t firstBlock, uint32_t lastBlock)
        {
            return HAL_SD_Erase(&handle, firstBlock, lastBlock);
        }

        HAL_StatusTypeDef SdCardHal::StartFlush([[maybe_unused]] Handle& handle)
        {
            return HAL_OK;
        }

        SdmmcCardState SdCardHal::CardState(Handle& handle)
        {
            auto state = HAL_SD_GetCardState(&handle);

            if (state == HAL_SD_CARD_TRANSFER)
                return SdmmcCardState::transfer;
            else if (state == HAL_SD_CARD_ERROR)
                return SdmmcCardState::error;
            else
                return SdmmcCardState::busy;
        }

        uint32_t SdCardHal::Error(Handle& handle)
        {
            return HAL_SD_GetError(&handle);
        }

        void SdCardHal::Abort(Handle& handle)
        {
            HAL_SD_Abort(&handle);
        }

        void SdCardHal::HandleInterrupt(Handle& handle)
        {
            HAL_SD_IRQHandler(&handle);
        }

        bool SdCardHal::RegisterCallbacks(Handle& handle, Callback transferred, Callback failed)
        {
            auto transmitStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_TX_CPLT_CB_ID, transferred);
            auto receiveStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_RX_CPLT_CB_ID, transferred);
            auto errorStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_ERROR_CB_ID, failed);
            auto abortStatus = HAL_SD_RegisterCallback(&handle, HAL_SD_ABORT_CB_ID, failed);
            return transmitStatus == HAL_OK && receiveStatus == HAL_OK && errorStatus == HAL_OK && abortStatus == HAL_OK;
        }

#if !defined(SD_EXTERNAL_DMA)
        HAL_StatusTypeDef MmcHal::ReadBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount)
        {
            return HAL_MMC_ReadBlocks_DMA(&handle, data, firstBlock, blockCount);
        }

        HAL_StatusTypeDef MmcHal::WriteBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount)
        {
            return HAL_MMC_WriteBlocks_DMA(&handle, data, firstBlock, blockCount);
        }

        HAL_StatusTypeDef MmcHal::Erase(Handle& handle, uint32_t firstBlock, uint32_t lastBlock)
        {
            // HAL_MMC_Erase erases whole erase groups, beyond the range, and a device without trim could take a trim for such an erase,
            // so the blocks keep their contents on such a device, which hal::BlockDevice allows
            if ((ExtCsdByte(handle, secureFeatureSupportIndex) & trimSupported) == 0)
                return HAL_OK;

            handle.ErrorCode = HAL_MMC_ERROR_NONE;
            return HAL_MMC_EraseSequence(&handle, HAL_MMC_TRIM, firstBlock, lastBlock);
        }

        HAL_StatusTypeDef MmcHal::StartFlush(Handle& handle)
        {
            if ((ExtCsdByte(handle, cacheControlIndex) & 1) == 0)
                return HAL_OK;

            return Switch(handle, flushCacheIndex, 1);
        }

        SdmmcCardState MmcHal::CardState(Handle& handle)
        {
            auto state = HAL_MMC_GetCardState(&handle);

            // A rejected CMD6 is reported only in the SWITCH_ERROR bit of the next status, which HAL_MMC_GetCardState drops
            if ((SDMMC_GetResponse(handle.Instance, SDMMC_RESP1) & switchErrorStatus) != 0)
                return SdmmcCardState::error;
            else if (state == HAL_MMC_CARD_TRANSFER)
                return SdmmcCardState::transfer;
            else
                return SdmmcCardState::busy;
        }

        uint32_t MmcHal::Error(Handle& handle)
        {
            return HAL_MMC_GetError(&handle);
        }

        void MmcHal::Abort(Handle& handle)
        {
            HAL_MMC_Abort(&handle);
        }

        void MmcHal::HandleInterrupt(Handle& handle)
        {
            HAL_MMC_IRQHandler(&handle);
        }

        bool MmcHal::RegisterCallbacks(Handle& handle, Callback transferred, Callback failed)
        {
            auto transmitStatus = HAL_MMC_RegisterCallback(&handle, HAL_MMC_TX_CPLT_CB_ID, transferred);
            auto receiveStatus = HAL_MMC_RegisterCallback(&handle, HAL_MMC_RX_CPLT_CB_ID, transferred);
            auto errorStatus = HAL_MMC_RegisterCallback(&handle, HAL_MMC_ERROR_CB_ID, failed);
            auto abortStatus = HAL_MMC_RegisterCallback(&handle, HAL_MMC_ABORT_CB_ID, failed);
            return transmitStatus == HAL_OK && receiveStatus == HAL_OK && errorStatus == HAL_OK && abortStatus == HAL_OK;
        }

        uint8_t MmcHal::ExtCsdByte(const Handle& handle, uint32_t index)
        {
            return static_cast<uint8_t>(handle.Ext_CSD[index / 4] >> (8 * (index % 4)));
        }

        HAL_StatusTypeDef MmcHal::Switch(Handle& handle, uint32_t index, uint8_t value)
        {
            constexpr uint32_t writeByte = 3;

            auto error = SDMMC_CmdSwitch(handle.Instance, (writeByte << 24) | (index << 16) | (uint32_t{ value } << 8));
            handle.ErrorCode = error;

            return error == HAL_MMC_ERROR_NONE ? HAL_OK : HAL_ERROR;
        }
#endif
    }

    template<class CardHal>
    SdmmcBlockDeviceStm<CardHal>::SdmmcBlockDeviceStm(const Config& config)
        : configuration{ config }
    {
        really_assert(config.busyPollInterval.count() > 0);
        really_assert(config.maxBlocksPerTransfer >= 1 && config.maxBlocksPerTransfer <= Config::maxBlocksPerTransferLimit);

        handle.owner = this;
    }

    template<class CardHal>
    uint32_t SdmmcBlockDeviceStm<CardHal>::BlockSize() const
    {
        return blockSize;
    }

    template<class CardHal>
    uint32_t SdmmcBlockDeviceStm<CardHal>::NumberOfBlocks() const
    {
        return numberOfBlocks;
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
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

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
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

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        Begin(Operation::erase, onDone);

        if (!cardPresent)
            Complete(Result::notPresent);
        else if (beginBlock > endBlock || endBlock > numberOfBlocks)
            Complete(Result::outOfRange);
        else if (beginBlock == endBlock)
            Complete(Result::success);
        else if (CardHal::Erase(handle, beginBlock, endBlock - 1) != HAL_OK)
            Complete(MapError(CardHal::Error(handle)));
        else
            WaitForCard();
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::Flush(const infra::Function<void(Result)>& onDone)
    {
        Begin(Operation::flush, onDone);

        if (!cardPresent)
            Complete(Result::notPresent);
        else if (CardHal::StartFlush(handle) != HAL_OK)
            Complete(MapError(CardHal::Error(handle)));
        else
            WaitForCard();
    }

    template<class CardHal>
    typename CardHal::Handle& SdmmcBlockDeviceStm<CardHal>::Handle()
    {
        return handle;
    }

    template<class CardHal>
    const typename CardHal::Handle& SdmmcBlockDeviceStm<CardHal>::Handle() const
    {
        return handle;
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::Activate(uint32_t cardBlocks, IRQn_Type irq)
    {
        auto registered = CardHal::RegisterCallbacks(handle, &SdmmcBlockDeviceStm::OnTransferred, &SdmmcBlockDeviceStm::OnFailed);
        really_assert(registered);

        numberOfBlocks = cardBlocks;
        handle.ErrorCode = SDMMC_ERROR_NONE;
        cardPresent = true;

        interrupt.emplace(irq, configuration.priority, [this]
            {
                CardHal::HandleInterrupt(handle);
            });
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::Deactivate()
    {
        interrupt.reset();
        timer.Cancel();

        if (phase == Phase::transferring)
            CardHal::Abort(handle);
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::OnTransferred(typename CardHal::Handle* halHandle)
    {
        static_cast<OwnedHandle*>(halHandle)->owner->PostEvent(Event::transferred, SDMMC_ERROR_NONE);
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::OnFailed(typename CardHal::Handle* halHandle)
    {
        static_cast<OwnedHandle*>(halHandle)->owner->PostEvent(Event::failed, CardHal::Error(*halHandle));
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::Begin(Operation newOperation, const infra::Function<void(Result)>& newOnDone)
    {
        really_assert(!onDone);
        onDone = newOnDone;
        ++operationId;
        operation = newOperation;
        phase = Phase::idle;
        readBuffer = infra::ByteRange();
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::AssertBuffer(const uint8_t* address, std::size_t size) const
    {
        auto location = reinterpret_cast<uintptr_t>(address);

        really_assert(size % blockSize == 0);
        really_assert(location % wordSize == 0);
        really_assert(!DataCacheEnabled() || (location % dataCacheLineSize == 0 && size % dataCacheLineSize == 0));
    }

    template<class CardHal>
    bool SdmmcBlockDeviceStm<CardHal>::IsValidRange(uint32_t firstBlock, uint32_t blockCount) const
    {
        return firstBlock <= numberOfBlocks && blockCount <= numberOfBlocks - firstBlock;
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::StartTransfer(uint8_t* buffer, uint32_t firstBlock, uint32_t blockCount)
    {
        cursor = buffer;
        nextBlock = firstBlock;
        blocksLeft = blockCount;
        StartChunk();
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::StartChunk()
    {
        chunkBlocks = std::min(blocksLeft, configuration.maxBlocksPerTransfer);

        auto status = operation == Operation::read
                          ? CardHal::ReadBlocks(handle, cursor, nextBlock, chunkBlocks)
                          : CardHal::WriteBlocks(handle, cursor, nextBlock, chunkBlocks);

        if (status != HAL_OK)
        {
            Complete(MapError(CardHal::Error(handle)));
            return;
        }

        phase = Phase::transferring;
        timer.Start(configuration.transferTimeout, [this]
            {
                OnTransferTimeout();
            });
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::ChunkTransferred()
    {
        cursor += chunkBlocks * blockSize;
        nextBlock += chunkBlocks;
        blocksLeft -= chunkBlocks;

        if (operation == Operation::write)
            WaitForCard();
        else
            ContinueOrComplete();
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::ContinueOrComplete()
    {
        if (blocksLeft == 0)
            Complete(Result::success);
        else
            StartChunk();
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::WaitForCard()
    {
        phase = Phase::waitingForCard;
        remainingPolls = static_cast<uint32_t>(configuration.busyTimeout / configuration.busyPollInterval);
        PollCard();
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::PollCard()
    {
        handle.ErrorCode = SDMMC_ERROR_NONE;
        auto state = CardHal::CardState(handle);

        if (handle.ErrorCode != SDMMC_ERROR_NONE)
            Complete(MapError(handle.ErrorCode));
        else if (state == detail::SdmmcCardState::transfer && operation == Operation::write)
            ContinueOrComplete();
        else if (state == detail::SdmmcCardState::transfer)
            Complete(Result::success);
        else if (state == detail::SdmmcCardState::error)
            Complete(Result::failed);
        else if (remainingPolls == 0)
            Complete(Result::timeout);
        else
        {
            --remainingPolls;
            timer.Start(configuration.busyPollInterval, [this]
                {
                    PollCard();
                });
        }
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::OnTransferTimeout()
    {
        CardHal::Abort(handle);
        Complete(Result::timeout);
    }

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::PostEvent(Event event, uint32_t errorCode)
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

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::HandleEvent()
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

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::Complete(Result result)
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

    template<class CardHal>
    void SdmmcBlockDeviceStm<CardHal>::Finish()
    {
        if (operation == Operation::read)
            InvalidateDataCache(readBuffer.begin(), readBuffer.size());

        operation = Operation::none;
        phase = Phase::idle;
        onDone(completionResult);
    }

    template class SdmmcBlockDeviceStm<detail::SdCardHal>;
#if !defined(SD_EXTERNAL_DMA)
    template class SdmmcBlockDeviceStm<detail::MmcHal>;
#endif
}

#endif
