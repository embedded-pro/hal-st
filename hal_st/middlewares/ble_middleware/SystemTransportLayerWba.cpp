#include "hal_st/middlewares/ble_middleware/SystemTransportLayerWba.hpp"
#include "hal_st/middlewares/ble_middleware/BleStackProcessWba.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>
#include <array>

extern "C"
{
#include "ble_bufsize.h"
#include "ble_common.h"
#include "blestack.h"
}

extern "C"
{
    SVCCTL_UserEvtFlowStatus_t SVCCTL_App_Notification(void* packet)
    {
        assert(packet != nullptr);

        auto& event = *reinterpret_cast<hci_event_pckt*>(static_cast<hci_uart_pckt*>(packet)->data);
        hal::SystemTransportLayerWba::Instance().HciEventHandler(event);

        return SVCCTL_UserEvtFlowEnable;
    }

    tBleStatus BLECB_Indication(const uint8_t* data, uint16_t length, const uint8_t*, uint16_t)
    {
        if (data[0] == HCI_EVENT_PKT_TYPE)
            return hal::SystemTransportLayerWba::Instance().QueueEvent(infra::ConstByteRange(data, data + length)) ? BLE_STATUS_SUCCESS : BLE_STATUS_FAILED;
        else if (data[0] == HCI_ACLDATA_PKT_TYPE)
            return BLE_STATUS_SUCCESS;

        return BLE_STATUS_FAILED;
    }
}

namespace
{
    const uint8_t maxNumberOfConnectionOrientedChannels = 32;
    const uint8_t bleStackOptions = 0;

    constexpr uint8_t PrepareWriteListSize(uint16_t maxAttMtuSize)
    {
        return static_cast<uint8_t>(BLE_PREP_WRITE_X_ATT(maxAttMtuSize));
    }
}

namespace hal
{
    SystemTransportLayerWba::SystemTransportLayerWba(infra::MemoryRange<uint32_t> stackBuffer, infra::MemoryRange<uint32_t> gattBuffer, uint8_t numberOfLinks, uint16_t mblockCount, uint16_t maxAttMtuSize)
    {
        really_assert(maxAttMtuSize >= BLE_DEFAULT_ATT_MTU && maxAttMtuSize <= maxAttMtuSizeLimit);
        really_assert(numberOfLinks != 0);

        BleStack_init_t bleStackInitParameters = {
            reinterpret_cast<uint8_t*>(stackBuffer.begin()),
            stackBuffer.size() * sizeof(uint32_t),
            reinterpret_cast<uint8_t*>(gattBuffer.begin()),
            gattBuffer.size() * sizeof(uint32_t),
            numberOfAttributeRecords,
            numberOfAttributeServices,
            attributeValueArraySize,
            numberOfLinks,
            PrepareWriteListSize(maxAttMtuSize),
            mblockCount,
            maxAttMtuSize,
            248,
            64,
            maxNumberOfConnectionOrientedChannels,
            bleStackOptions,
            0U
        };

        really_assert(BleStack_Init(&bleStackInitParameters) == BLE_STATUS_SUCCESS);
    }

    void SystemTransportLayerWba::HciEventHandler(hci_event_pckt& event)
    {
        infra::Subject<HciEventSink>::NotifyObservers([&event](auto& observer)
            {
                observer.HciEvent(event);
            });
    }

    bool SystemTransportLayerWba::QueueEvent(infra::ConstByteRange packet)
    {
        really_assert(packet.size() <= maxEventPacketSize);

        if (events.full())
        {
            eventFlowPaused = true;
            return false;
        }

        events.emplace_back();
        std::copy(packet.begin(), packet.end(), events.back().begin());

        if (events.size() == 1)
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    ProcessQueuedEvent();
                });

        return true;
    }

    void SystemTransportLayerWba::ProcessQueuedEvent()
    {
        auto packet = events.front();
        events.pop_front();

        if (!events.empty())
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    ProcessQueuedEvent();
                });

        if (eventFlowPaused)
        {
            eventFlowPaused = false;
            ResumeBleEventFlow();
        }

        SVCCTL_UserEvtRx(packet.data());
    }
}
