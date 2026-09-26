#include "hal_st/stm32fxxx/FlashCoordinatedWithWirelessStack.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "shci.h"
#include "stm32wbxx_ll_hsem.h"
#include <algorithm>
#include <utility>

namespace hal
{
    FlashCoordinatedWithWirelessStack::FlashCoordinatedWithWirelessStack(FlashInternalStmBase& flash, WatchDogStm& watchdog, WirelessStack wirelessStack)
        : flash(flash)
        , watchdog(watchdog)
        , wirelessStack(wirelessStack)
        , hwSemInterruptHandler(HSEM_IRQn, [this]()
              {
                  HsemInterruptHandler();
              })
        , nmiHandler(NonMaskableInt_IRQn, [this]()
              {
                  EccErrorHandler();
              })
    {
        if (wirelessStack == WirelessStack::running)
            SHCI_C2_SetFlashActivityControl(FLASH_ACTIVITY_CONTROL_SEM7);
    }

    void FlashCoordinatedWithWirelessStack::WirelessStackStarting()
    {
        really_assert(wirelessStack == WirelessStack::stopped);

        wirelessStack = WirelessStack::starting;
    }

    void FlashCoordinatedWithWirelessStack::WirelessStackReady()
    {
        really_assert(wirelessStack != WirelessStack::running);

        SHCI_C2_SetFlashActivityControl(FLASH_ACTIVITY_CONTROL_SEM7);
        wirelessStack = WirelessStack::running;
        TryStep();
    }

    void FlashCoordinatedWithWirelessStack::FirmwareUpgradeServicesReady()
    {
        wirelessStack = WirelessStack::stopped;
        TryStep();
    }

    uint32_t FlashCoordinatedWithWirelessStack::NumberOfSectors() const
    {
        return flash.NumberOfSectors();
    }

    uint32_t FlashCoordinatedWithWirelessStack::SizeOfSector(uint32_t sectorIndex) const
    {
        return flash.SizeOfSector(sectorIndex);
    }

    uint32_t FlashCoordinatedWithWirelessStack::SectorOfAddress(uint32_t address) const
    {
        return flash.SectorOfAddress(address);
    }

    uint32_t FlashCoordinatedWithWirelessStack::AddressOfSector(uint32_t sectorIndex) const
    {
        return flash.AddressOfSector(sectorIndex);
    }

    void FlashCoordinatedWithWirelessStack::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        this->buffer = buffer;
        this->address = address;
        this->onDone = onDone;
        operation = Operation::write;
        WriteNextDoubleWord();
    }

    void FlashCoordinatedWithWirelessStack::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        flash.ReadBuffer(buffer, address, onDone);
    }

    void FlashCoordinatedWithWirelessStack::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        currentSector = beginIndex;
        endSector = endIndex;
        this->onDone = onDone;
        operation = Operation::erase;
        EraseNextSector();
    }

    void FlashCoordinatedWithWirelessStack::WriteNextDoubleWord()
    {
        if (buffer.empty())
        {
            Finish();
            return;
        }

        StepWhenCpu2AllowsFlashAccess([this]()
            {
                const auto size = std::min<std::size_t>(buffer.size(), doubleWordSize - address % doubleWordSize);
                flash.WriteBuffer(infra::Head(buffer, size), address, [this]()
                    {
                        WriteNextDoubleWord();
                    });
                buffer.pop_front(size);
                address += size;
            });
    }

    void FlashCoordinatedWithWirelessStack::EraseNextSector()
    {
        if (currentSector == endSector)
        {
            if (std::exchange(eraseActivityReported, false))
                SHCI_C2_FLASH_EraseActivity(ERASE_ACTIVITY_OFF);

            Finish();
            return;
        }

        StepWhenCpu2AllowsFlashAccess([this]()
            {
                flash.EraseSectors(currentSector, currentSector + 1, [this]()
                    {
                        EraseNextSector();
                    });
                ++currentSector;
            });
    }

    void FlashCoordinatedWithWirelessStack::Finish()
    {
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                onDone();
            });
    }

    void FlashCoordinatedWithWirelessStack::StepWhenCpu2AllowsFlashAccess(const infra::Function<void()>& step)
    {
        this->step = step;
        stepPending = true;
        TryStep();
    }

    void FlashCoordinatedWithWirelessStack::TryStep()
    {
        if (!stepPending || wirelessStack == WirelessStack::starting)
            return;

        ReportEraseActivity();

        if (StepWithCpu2LockedOut())
            return;

        LL_HSEM_ClearFlag_C1ICR(HSEM, semaphoreMask);
        LL_HSEM_EnableIT_C1IER(HSEM, semaphoreMask);

        if (LL_HSEM_GetStatus(HSEM, hwBlockFlashReqByCpu2) == 0)
        {
            LL_HSEM_DisableIT_C1IER(HSEM, semaphoreMask);
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    TryStep();
                });
        }
    }

    void FlashCoordinatedWithWirelessStack::ReportEraseActivity()
    {
        if (operation == Operation::erase && wirelessStack == WirelessStack::running && !eraseActivityReported)
        {
            SHCI_C2_FLASH_EraseActivity(ERASE_ACTIVITY_ON);
            eraseActivityReported = true;
        }
    }

    bool FlashCoordinatedWithWirelessStack::StepWithCpu2LockedOut()
    {
        CriticalSectionScoped criticalSection(watchdog);

        if (LL_HSEM_1StepLock(HSEM, hwBlockFlashReqByCpu2) != 0)
            return false;

        stepPending = false;
        step();
        LL_HSEM_ReleaseLock(HSEM, hwBlockFlashReqByCpu2, 0);
        return true;
    }

    void FlashCoordinatedWithWirelessStack::HsemInterruptHandler()
    {
        if (LL_HSEM_IsActiveFlag_C1MISR(HSEM, semaphoreMask) == 0)
            return;

        LL_HSEM_DisableIT_C1IER(HSEM, semaphoreMask);
        LL_HSEM_ClearFlag_C1ICR(HSEM, semaphoreMask);
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                TryStep();
            });
    }

    void FlashCoordinatedWithWirelessStack::EccErrorHandler()
    {
        if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_ECCD))
        {
            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ECCD);
            const uint32_t errorAddress = READ_BIT(FLASH->ECCR, FLASH_ECCR_ADDR_ECC);
            uint32_t pageError = 0;
            FLASH_EraseInitTypeDef eraseInitStruct{ .TypeErase = FLASH_TYPEERASE_PAGES, .Page = flash.SectorOfAddress(errorAddress), .NbPages = 1 };
            HAL_FLASHEx_Erase(&eraseInitStruct, &pageError);
        }
    }

    FlashCoordinatedWithWirelessStack::CriticalSectionScoped::CriticalSectionScoped(WatchDogStm& watchdog)
        : primaskBit(__get_PRIMASK())
        , watchdog(watchdog)
    {
        __disable_irq();
        watchdog.WatchDogRefresh();
    }

    FlashCoordinatedWithWirelessStack::CriticalSectionScoped::~CriticalSectionScoped()
    {
        __set_PRIMASK(primaskBit);
        watchdog.WatchDogRefresh();
    }
}
