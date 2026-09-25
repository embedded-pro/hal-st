#include "hal_st/middlewares/ble_middleware/BleStackProcessWba.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <atomic>
#include <cstdint>

extern "C"
{
#include "auto/ble_raw_api.h"
#include "blestack.h"
#include "common_types.h"
#include "ll_intf.h"
#include "ll_sys.h"

    // Set by the link layer, possibly from its interrupt
    extern uint8_t missed_hci_event_flag;
}

namespace
{
    // Bluetooth Core Specification, Volume 4, Part E, section 7.7.16; ST reports a lost link layer event with this code
    constexpr uint8_t lostLinkLayerEventHardwareCode = 0x03;

    constexpr uint8_t allowAllLinkLayerEvents = 0x0f;
}

// The event dispatcher takes the place of ST's sequencer: the link layer background process and the
// host stack each run as a single pending action, however often they are requested.
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

                    if (std::atomic_ref<uint8_t>(missed_hci_event_flag).exchange(0) != 0)
                        HCI_HARDWARE_ERROR_EVENT(lostLinkLayerEventHardwareCode);

                    if (BleStack_Process() == BLE_SLEEPMODE_RUNNING)
                        BleStackCB_Process();
                });
    }

    void HostStack_Process()
    {
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
