#include "infra/event/EventDispatcher.hpp"
#include <atomic>
#include <cstdint>

extern "C"
{
#include "auto/ble_raw_api.h"
#include "blestack.h"
#include "ll_sys.h"

    extern uint8_t missed_hci_event_flag;
}

namespace
{
    // Bluetooth Core Specification, Volume 4, Part E, section 7.7.16; ST reports a lost link layer event with this code
    constexpr uint8_t lostLinkLayerEventHardwareCode = 0x03;
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

                    if (missed_hci_event_flag != 0)
                    {
                        missed_hci_event_flag = 0;
                        HCI_HARDWARE_ERROR_EVENT(lostLinkLayerEventHardwareCode);
                    }

                    if (BleStack_Process() == BLE_SLEEPMODE_RUNNING)
                        BleStackCB_Process();
                });
    }

    void HostStack_Process()
    {
        BleStackCB_Process();
    }
}
