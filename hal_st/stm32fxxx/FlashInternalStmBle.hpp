
#ifndef HAL_FLASH_INTERNAL_STM_BLE_HPP
#define HAL_FLASH_INTERNAL_STM_BLE_HPP

#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal_st/stm32fxxx/FlashInternalStm.hpp"
#include "hal_st/stm32fxxx/WatchDogStm.hpp"
#include "services/flash/FlashAlign.hpp"
#include <cstdint>

namespace hal
{
    class FlashInternalStmBle
        : public FlashHomogeneousInternalStm
    {
    public:
        enum class WirelessStack : uint8_t
        {
            running,
            starting
        };

        FlashInternalStmBle(uint32_t numberOfSectors, uint32_t sizeOfEachSector, infra::ConstByteRange flashMemory, WatchDogStm& watchdog, WirelessStack wirelessStack = WirelessStack::running);

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

        void WirelessStackReady();

    private:
        enum class HeldOperation : uint8_t
        {
            none,
            write,
            erase
        };

        void StartWrite(infra::ConstByteRange buffer, uint32_t address);
        void StartErase(uint32_t beginIndex, uint32_t endIndex);
        void StartHeldOperation();

        enum class FlashOperation
        {
            write,
            erase
        };

        void WaitForCpu2AllowFlashOperation(infra::Function<void()> onAvailable);
        void HsemInterruptHandler();
        void EccErrorHandler();
        void TryWrite();
        void TryErase();
        bool SingleOperation(FlashOperation operation);
        void SingleWrite();
        void SingleErase();

        class CriticalSectionScoped
        {
        public:
            CriticalSectionScoped(WatchDogStm& watchdog);
            ~CriticalSectionScoped();

        private:
            uint32_t primaskBit;
            WatchDogStm& watchdog;
        };

        static constexpr uint32_t hwBlockFlashReqByCpu2 = 7;
        infra::ConstByteRange flashMemory;
        WatchDogStm& watchdog;
        cortex::ImmediateInterruptHandler hwSemInterruptHandler;
        cortex::ImmediateInterruptHandler nmiHandler;
        infra::Function<void()> onHwSemaphoreAvailable;

        services::FlashAlign::WithAlignment<sizeof(uint64_t)> flashAlign;
        services::FlashAlign::Chunk* chunkToWrite;
        infra::Function<void()> onWriteDone;

        uint32_t currentEraseIndex;
        uint32_t endEraseIndex;
        infra::Function<void()> onEraseDone;

        bool wirelessStackReady = false;
        HeldOperation heldOperation = HeldOperation::none;
        infra::ConstByteRange heldBuffer;
        uint32_t heldAddress = 0;
    };
}

#endif
