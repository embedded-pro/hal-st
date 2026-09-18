#include "hal_st/middlewares/ble_middleware/SystemTransportLayerWba.hpp"
#include <array>
extern "C"
{
#include "ble_common.h"
#include "ble_bufsize.h"
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

    tBleStatus ProcessEventPacket(const uint8_t* data)
    {
        SVCCTL_UserEvtRx(const_cast<uint8_t *>(data));
        return BLE_STATUS_SUCCESS;
    }

    tBleStatus BLECB_Indication(const uint8_t* data, uint16_t, const uint8_t*, uint16_t)
    {
        if (data[0] == HCI_EVENT_PKT_TYPE)
            return ProcessEventPacket(data);
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
}
