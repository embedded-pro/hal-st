#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
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
    class SdCardStm
        : public BlockDevice
    {
    public:
        enum class BusWidth : uint8_t
        {
            oneBit,
            fourBit
        };

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

            BusWidth busWidth{ BusWidth::fourBit };
            uint32_t clockDivider{ defaultClockDivider };
            cortex::InterruptPriority priority{ cortex::InterruptPriority::normal };
            std::chrono::milliseconds transferTimeout{ 1000 };
            std::chrono::milliseconds busyTimeout{ 5000 };
            std::chrono::milliseconds busyPollInterval{ 1 };
            uint32_t maxBlocksPerTransfer{ maxBlocksPerTransferLimit };
#if defined(SD_EXTERNAL_DMA)
            DmaChannelId dma{ 2, 3, 4 };
#endif
        };

        struct DirectionPins
        {
            DirectionPins()
                : data0{ dummyPinStm }
                , data123{ dummyPinStm }
                , command{ dummyPinStm }
            {}

            DirectionPins(GpioPinStm& data0, GpioPinStm& data123, GpioPinStm& command)
                : data0{ data0 }
                , data123{ data123 }
                , command{ command }
            {}

            GpioPinStm& data0;
            GpioPinStm& data123;
            GpioPinStm& command;
        };

        SdCardStm(uint8_t oneBasedIndex, GpioPinStm& clock, GpioPinStm& command, GpioPinStm& data0, GpioPinStm& data1 = dummyPinStm, GpioPinStm& data2 = dummyPinStm, GpioPinStm& data3 = dummyPinStm, const Config& config = Config(), const DirectionPins& directionPins = DirectionPins{});
        ~SdCardStm();
        SdCardStm(const SdCardStm& other) = delete;
        SdCardStm& operator=(const SdCardStm& other) = delete;

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;

    private:
        struct Handle
            : SD_HandleTypeDef
        {
            SdCardStm* owner{ nullptr };
        };

        enum class Operation : uint8_t
        {
            none,
            read,
            write,
            erase
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

        static void OnReceiveComplete(SD_HandleTypeDef* handle);
        static void OnTransmitComplete(SD_HandleTypeDef* handle);
        static void OnError(SD_HandleTypeDef* handle);
        static void OnAbort(SD_HandleTypeDef* handle);

        void InitSdmmc();
        void RegisterCallbacks();
        void RegisterInterrupts();
#if defined(SD_EXTERNAL_DMA)
        void InitDma();
#endif

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
        PeripheralPinStm clock;
        PeripheralPinStm command;
        PeripheralPinStm data0;
        PeripheralPinStm data1;
        PeripheralPinStm data2;
        PeripheralPinStm data3;
        PeripheralPinStm data0Direction;
        PeripheralPinStm data123Direction;
        PeripheralPinStm commandDirection;
        uint8_t oneBasedIndex;
        Config config;
        Handle handle{};
#if defined(SD_EXTERNAL_DMA)
        DMA_HandleTypeDef dma{};
        bool dmaInitialized{ false };
#endif
        bool cardPresent{ false };
        uint32_t blockSize{ 512 };
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
        std::optional<cortex::DispatchedInterruptHandler> sdInterrupt;
#if defined(SD_EXTERNAL_DMA)
        std::optional<cortex::DispatchedInterruptHandler> dmaInterrupt;
#endif
    };
}

#endif
