#include "hal_st/stm32fxxx/BackupRamStm.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"

namespace hal
{
    BackupRamStm::BackupRamStm()
    {
#if defined(STM32WBA)
        __HAL_RCC_RTCAPB_CLK_ENABLE();
        HAL_PWR_EnableBkUpAccess();
#endif
    }

    infra::MemoryRange<volatile uint32_t> BackupRamStm::Get() const
    {
#if defined(STM32G0)
        return infra::MakeRange(&TAMP->BKP0R, &TAMP->BKP4R + 1);
#elif defined(STM32G4) || defined(STM32WBA) || defined(STM32H5)
        return infra::MakeRange(&TAMP->BKP0R, &TAMP->BKP15R + 1);
#else
        return infra::MakeRange(&peripheralRtc[0]->BKP0R, &peripheralRtc[0]->BKP19R + 1);
#endif
    }
}
