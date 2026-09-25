#ifndef HAL_FLASH_COORDINATED_WITH_WIRELESS_STACK_HPP
#define HAL_FLASH_COORDINATED_WITH_WIRELESS_STACK_HPP

#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/Flash.hpp"
#include "hal_st/stm32fxxx/FlashInternalStm.hpp"
#include "hal_st/stm32fxxx/WatchDogStm.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include <cstdint>

namespace hal
{
    class FlashCoordinatedWithWirelessStack
        : public hal::Flash
    {
    public:
        enum class WirelessStack : uint8_t
        {
            stopped,
            starting,
            running
        };

        FlashCoordinatedWithWirelessStack(FlashInternalStmBase& flash, WatchDogStm& watchdog, WirelessStack wirelessStack = WirelessStack::running);

        void WirelessStackStarting();
        void WirelessStackReady();

        uint32_t NumberOfSectors() const override;
        uint32_t SizeOfSector(uint32_t sectorIndex) const override;
        uint32_t SectorOfAddress(uint32_t address) const override;
        uint32_t AddressOfSector(uint32_t sectorIndex) const override;

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

    private:
        enum class Operation : uint8_t
        {
            write,
            erase
        };

        class CriticalSectionScoped
        {
        public:
            explicit CriticalSectionScoped(WatchDogStm& watchdog);
            ~CriticalSectionScoped();

        private:
            uint32_t primaskBit;
            WatchDogStm& watchdog;
        };

        void WriteNextDoubleWord();
        void EraseNextSector();
        void Finish();
        void StepWhenCpu2AllowsFlashAccess(const infra::Function<void()>& step);
        void TryStep();
        void ReportEraseActivity();
        bool StepWithCpu2LockedOut();
        void HsemInterruptHandler();
        void EccErrorHandler();

        static constexpr uint32_t hwBlockFlashReqByCpu2 = 7;
        static constexpr uint32_t semaphoreMask = 1u << hwBlockFlashReqByCpu2;
        static constexpr uint32_t doubleWordSize = sizeof(uint64_t);

        FlashInternalStmBase& flash;
        WatchDogStm& watchdog;
        WirelessStack wirelessStack;
        cortex::ImmediateInterruptHandler hwSemInterruptHandler;
        cortex::ImmediateInterruptHandler nmiHandler;

        Operation operation = Operation::write;
        bool eraseActivityReported = false;
        infra::ConstByteRange buffer;
        uint32_t address = 0;
        uint32_t currentSector = 0;
        uint32_t endSector = 0;
        infra::Function<void()> step;
        bool stepPending = false;
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
