#include "hal_st/middlewares/ble_middleware/BleStackProcessWba.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <array>
#include <atomic>
#include <cstdint>

extern "C"
{
#include "ble_std.h"
#include "blestack.h"
#include "common_types.h"
#include "ll_intf.h"
#include "ll_sys.h"
#include "ll_sys_startup.h"
}

namespace
{
    constexpr uint8_t lostLinkLayerEventHardwareCode = 0x03;

    constexpr uint8_t allowAllLinkLayerEvents = 0x0f;

    constexpr std::array<uint8_t, 4> lostLinkLayerEvent{ HCI_EVENT_PKT_TYPE, HCI_HARDWARE_ERROR_EVT_CODE, 1, lostLinkLayerEventHardwareCode };

    // Set by the link layer, possibly from its interrupt
    std::atomic_bool linkLayerEventMissed{ false };
}

extern "C"
{
    void ll_sys_bg_process_init()
    {}

    void ll_sys_schedule_bg_process()
    {
        static std::atomic_bool scheduled{ false };

        if (!scheduled.exchange(true))
            infra::EventDispatcher::Instance().Schedule([]()
                {
                    scheduled = false;
                    ll_sys_bg_process();
                });
    }

    void ll_sys_schedule_bg_process_isr()
    {
        ll_sys_schedule_bg_process();
    }

    void BleStackCB_Process()
    {
        static std::atomic_bool scheduled{ false };

        if (!scheduled.exchange(true))
            infra::EventDispatcher::Instance().Schedule([]()
                {
                    scheduled = false;

                    if (linkLayerEventMissed.exchange(false) && BLECB_Indication(lostLinkLayerEvent.data(), lostLinkLayerEvent.size(), nullptr, 0) != BLE_STATUS_SUCCESS)
                        linkLayerEventMissed = true;

                    if (BleStack_Process() == BLE_SLEEPMODE_RUNNING)
                        BleStackCB_Process();
                });
    }

    void HostStack_Process()
    {
        BleStackCB_Process();
    }

    void ll_sys_handle_missed_event_cb(uint16_t, uint8_t*)
    {
        linkLayerEventMissed = true;
        BleStackCB_Process();
    }
}

namespace hal
{
    void ResumeBleEventFlow()
    {
        change_state_options_t options{};
        options.combined_value = allowAllLinkLayerEvents;
        ll_intf_chng_evnt_hndlr_state(options);

        BleStackCB_Process();
    }
}
