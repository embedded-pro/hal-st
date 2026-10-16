#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_SD)

#if !defined(SDMMC_IDMA_IDMAEN)
#define SD_EXTERNAL_DMA
#endif

namespace hal
{
    namespace detail
    {
        enum class SdmmcCardState : uint8_t
        {
            transfer,
            busy,
            error
        };

        struct SdCardHal
        {
            using Handle = SD_HandleTypeDef;
            using Callback = void (*)(Handle* handle);

            static HAL_StatusTypeDef ReadBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount);
            static HAL_StatusTypeDef WriteBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount);
            static HAL_StatusTypeDef Erase(Handle& handle, uint32_t firstBlock, uint32_t lastBlock);
            static HAL_StatusTypeDef StartFlush(Handle& handle);
            static SdmmcCardState CardState(Handle& handle);
            static uint32_t Error(Handle& handle);
            static void Abort(Handle& handle);
            static void HandleInterrupt(Handle& handle);
            static bool RegisterCallbacks(Handle& handle, Callback transferred, Callback failed);
        };

#if !defined(SD_EXTERNAL_DMA)
        struct MmcHal
        {
            using Handle = MMC_HandleTypeDef;
            using Callback = void (*)(Handle* handle);

            static constexpr uint32_t flushCacheIndex = 32;
            static constexpr uint32_t cacheControlIndex = 33;
            static constexpr uint32_t highCapacityEraseGroupSizeIndex = 224;
            static constexpr uint32_t secureFeatureSupportIndex = 231;
            static constexpr uint32_t cacheSizeIndex = 249;

            static HAL_StatusTypeDef ReadBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount);
            static HAL_StatusTypeDef WriteBlocks(Handle& handle, uint8_t* data, uint32_t firstBlock, uint32_t blockCount);
            static HAL_StatusTypeDef Erase(Handle& handle, uint32_t firstBlock, uint32_t lastBlock);
            static HAL_StatusTypeDef StartFlush(Handle& handle);
            static SdmmcCardState CardState(Handle& handle);
            static uint32_t Error(Handle& handle);
            static void Abort(Handle& handle);
            static void HandleInterrupt(Handle& handle);
            static bool RegisterCallbacks(Handle& handle, Callback transferred, Callback failed);

            static uint8_t ExtCsdByte(const Handle& handle, uint32_t index);
            static HAL_StatusTypeDef Switch(Handle& handle, uint32_t index, uint8_t value);
        };
#endif
    }

    template<class CardHal>
    class SdmmcBlockDeviceStm
        : public BlockDevice
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

#if defined(SD_EXTERNAL_DMA)
            // 12 MHz from the 48 MHz kernel clock of F4 and F7
            static constexpr uint32_t defaultClockDivider = 2;
            // The HAL programs the DMA with 16-bit word counts
            static constexpr uint32_t maxBlocksPerTransferLimit = 511;
#else
            // At most 25 MHz from the 200 MHz maximum kernel clock of H5 and H7
            static constexpr uint32_t defaultClockDivider = 4;
            // The SDMMC data length is 25 bits
            static constexpr uint32_t maxBlocksPerTransferLimit = 65535;
#endif

            uint32_t clockDivider{ defaultClockDivider };
            cortex::InterruptPriority priority{ cortex::InterruptPriority::normal };
            std::chrono::milliseconds transferTimeout{ 1000 };
            std::chrono::milliseconds busyTimeout{ 5000 };
            std::chrono::milliseconds busyPollInterval{ 1 };
            uint32_t maxBlocksPerTransfer{ maxBlocksPerTransferLimit };
        };

        SdmmcBlockDeviceStm(const SdmmcBlockDeviceStm& other) = delete;
        SdmmcBlockDeviceStm& operator=(const SdmmcBlockDeviceStm& other) = delete;

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

    protected:
        static constexpr uint32_t blockSize = 512;

        explicit SdmmcBlockDeviceStm(const Config& config);
        ~SdmmcBlockDeviceStm() = default;

        typename CardHal::Handle& Handle();
        const typename CardHal::Handle& Handle() const;

        // The derived class activates once the card is identified, and deactivates before it de-initialises the HAL
        void Activate(uint32_t cardBlocks, IRQn_Type irq);
        void Deactivate();

    private:
        struct OwnedHandle
            : CardHal::Handle
        {
            SdmmcBlockDeviceStm* owner{ nullptr };
        };

        enum class Operation : uint8_t
        {
            none,
            read,
            write,
            erase,
            flush
        };

        enum class Phase : uint8_t
        {
            idle,
            transferring,
            waitingForCard,
            completing
        };

        enum class Event : uint8_t
        {
            none,
            transferred,
            failed
        };

        static void OnTransferred(typename CardHal::Handle* halHandle);
        static void OnFailed(typename CardHal::Handle* halHandle);

        void Begin(Operation newOperation, const infra::Function<void(Result)>& newOnDone);
        void AssertBuffer(const uint8_t* address, std::size_t size) const;
        bool IsValidRange(uint32_t firstBlock, uint32_t blockCount) const;
        void StartTransfer(uint8_t* buffer, uint32_t firstBlock, uint32_t blockCount);
        void StartChunk();
        void ChunkTransferred();
        void ContinueOrComplete();
        void WaitForCard();
        void PollCard();
        void OnTransferTimeout();
        void PostEvent(Event event, uint32_t errorCode);
        void HandleEvent();
        void Complete(Result result);
        void Finish();

    private:
        Config configuration;
        OwnedHandle handle{};
        bool cardPresent{ false };
        uint32_t numberOfBlocks{ 0 };

        infra::AutoResetFunction<void(Result)> onDone;
        Operation operation{ Operation::none };
        Phase phase{ Phase::idle };
        uint8_t* cursor{ nullptr };
        uint32_t nextBlock{ 0 };
        uint32_t blocksLeft{ 0 };
        uint32_t chunkBlocks{ 0 };
        infra::ByteRange readBuffer;
        Result completionResult{ Result::success };
        uint32_t remainingPolls{ 0 };
        Event pendingEvent{ Event::none };
        uint32_t pendingErrorCode{ 0 };
        uint32_t pendingEventOperation{ 0 };
        uint32_t operationId{ 0 };
        bool eventScheduled{ false };
        infra::TimerSingleShot timer;
        std::optional<cortex::DispatchedInterruptHandler> interrupt;
    };

    extern template class SdmmcBlockDeviceStm<detail::SdCardHal>;
#if !defined(SD_EXTERNAL_DMA)
    extern template class SdmmcBlockDeviceStm<detail::MmcHal>;
#endif
}

#endif
