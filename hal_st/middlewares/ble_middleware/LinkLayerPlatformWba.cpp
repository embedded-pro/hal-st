#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <cstdint>

extern "C"
{
#include "linklayer_plat.h"
#include "ll_sys.h"
}

// Stubs so the link layer links; each hook gets its real implementation in a later step.
extern "C"
{
    void LINKLAYER_PLAT_ClockInit()
    {}

    void LINKLAYER_PLAT_DelayUs(uint32_t)
    {}

    void LINKLAYER_PLAT_Assert(uint8_t condition)
    {
        really_assert(condition != 0);
    }

    void LINKLAYER_PLAT_AclkCtrl(uint8_t)
    {}

    void LINKLAYER_PLAT_NotifyWFIEnter()
    {}

    void LINKLAYER_PLAT_NotifyWFIExit()
    {}

    void LINKLAYER_PLAT_WaitHclkRdy()
    {}

    void LINKLAYER_PLAT_GetRNG(uint8_t* ptr_rnd, uint32_t len)
    {
        std::fill_n(ptr_rnd, len, 0);
    }

    void LINKLAYER_PLAT_SetupRadioIT(void (*)())
    {}

    void LINKLAYER_PLAT_SetupSwLowIT(void (*)())
    {}

    void LINKLAYER_PLAT_TriggerSwLowIT(uint8_t)
    {}

    void LINKLAYER_PLAT_EnableIRQ()
    {}

    void LINKLAYER_PLAT_DisableIRQ()
    {}

    void LINKLAYER_PLAT_EnableSpecificIRQ(uint8_t)
    {}

    void LINKLAYER_PLAT_DisableSpecificIRQ(uint8_t)
    {}

    void LINKLAYER_PLAT_EnableRadioIT()
    {}

    void LINKLAYER_PLAT_DisableRadioIT()
    {}

    void LINKLAYER_PLAT_StartRadioEvt()
    {}

    void LINKLAYER_PLAT_StopRadioEvt()
    {}

    void LINKLAYER_PLAT_RCOStartClbr()
    {}

    void LINKLAYER_PLAT_RCOStopClbr()
    {}

    void LINKLAYER_PLAT_RequestTemperature()
    {}

    void LINKLAYER_PLAT_EnableOSContextSwitch()
    {}

    void LINKLAYER_PLAT_DisableOSContextSwitch()
    {}

    void LINKLAYER_PLAT_SCHLDR_TIMING_UPDATE_NOT(Evnt_timing_t*)
    {}

    uint32_t LINKLAYER_PLAT_GetSTCompanyID()
    {
        return 0;
    }

    uint32_t LINKLAYER_PLAT_GetUDN()
    {
        return 0;
    }

    // ST declares these with an enum of debug signals from its RTDebug module, which hal-st does not ship.
    void LINKLAYER_DEBUG_SIGNAL_SET(int)
    {}

    void LINKLAYER_DEBUG_SIGNAL_RESET(int)
    {}

    void LINKLAYER_DEBUG_SIGNAL_TOGGLE(int)
    {}

    void ll_sys_reset()
    {}

    void ll_sys_config_params()
    {}
}
