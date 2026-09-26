#include "hal_st/middlewares/ble_middleware/SystemTransportLayerWba.hpp"
#include "hal_st/middlewares/ble_middleware/BleStackProcessWba.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>
#include <array>

extern "C"
{
#include "ble_bufsize.h"
#include "ble_common.h"
#include "ble_gen_aci.h"
#include "ble_hal_aci.h"
#include "ble_hci_le.h"
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
    constexpr uint16_t maxConnectionOrientedChannelPduSize = 248;
    constexpr uint8_t maxConnectionOrientedChannels = 64;
    constexpr uint8_t maxConnectionOrientedChannelsAsInitiator = 32;
    constexpr uint16_t bleStackOptions = 0;

    constexpr uint8_t PrepareWriteListSize(uint16_t maxAttMtuSize)
    {
        return static_cast<uint8_t>(BLE_PREP_WRITE_X_ATT(maxAttMtuSize));
    }

    BleStack_init_t StackParameters(infra::MemoryRange<uint32_t> stackBuffer, infra::MemoryRange<uint32_t> gattBuffer, infra::MemoryRange<uint16_t> hostEvents, infra::MemoryRange<uint8_t> gattLongWrite, infra::MemoryRange<uint64_t> nvmCache, uint8_t numberOfLinks, uint16_t mblockCount, uint16_t maxAttMtuSize)
    {
        return BleStack_init_t{
            .bleStartRamAddress = reinterpret_cast<uint8_t*>(stackBuffer.begin()),
            .total_buffer_size = stackBuffer.size() * sizeof(uint32_t),
            .nvm_cache_buffer = nvmCache.begin(),
            .nvm_cache_size = static_cast<uint16_t>(nvmCache.size() - 1),
            .nvm_cache_max_size = static_cast<uint16_t>(nvmCache.size()),
            .bleStartRamAddress_GATT = reinterpret_cast<uint8_t*>(gattBuffer.begin()),
            .total_buffer_size_GATT = gattBuffer.size() * sizeof(uint32_t),
            .gatt_long_write_buffer = gattLongWrite.begin(),
            .extra_data_buffer = nullptr,
            .extra_data_buffer_size = 0,
            .host_event_fifo_buffer = hostEvents.begin(),
            .host_event_fifo_buffer_size = static_cast<uint16_t>(hostEvents.size()),
            .numAttrRecord = hal::SystemTransportLayerWba::numberOfAttributeRecords,
            .numAttrServ = hal::SystemTransportLayerWba::numberOfAttributeServices,
            .attrValueArrSize = hal::SystemTransportLayerWba::attributeValueArraySize,
            .numOfLinks = numberOfLinks,
            .prWriteListSize = PrepareWriteListSize(maxAttMtuSize),
            .mblockCount = mblockCount,
            .max_add_eatt_bearers = hal::SystemTransportLayerWba::additionalEattBearers,
            .attMtu = maxAttMtuSize,
            .max_coc_mps = maxConnectionOrientedChannelPduSize,
            .max_coc_nbr = maxConnectionOrientedChannels,
            .max_coc_initiator_nbr = maxConnectionOrientedChannelsAsInitiator,
            .options = bleStackOptions,
            .debug = 0,
        };
    }
}

namespace hal
{
    SystemTransportLayerWba::SystemTransportLayerWba(const StackMemory& memory, infra::MemoryRange<BlePlatformWba::TimerSlot> timers, infra::MemoryRange<uint64_t> nvmStorage, services::ConfigurationStoreAccess<infra::ByteRange> bondBlob, const HardwareDependencies& hardware, const StackConfig& config)
        : linkLayerPlatform(hardware.randomDataGenerator, config.linkLayer)
        , blePlatform(timers, hardware.aes, hardware.pka)
        , nvm(nvmStorage, bondBlob)
    {
        really_assert(config.maxAttMtuSize >= BLE_DEFAULT_ATT_MTU && config.maxAttMtuSize <= maxAttMtuSizeLimit);
        really_assert(config.numberOfLinks != 0);

        auto parameters = StackParameters(memory.stack, memory.gatt, memory.hostEvents, memory.gattLongWrite, nvm.Cache(), config.numberOfLinks, config.mblockCount, config.maxAttMtuSize);
        really_assert(BleStack_Init(&parameters) == BLE_STATUS_SUCCESS);
    }

    SystemTransportLayerWba::Version SystemTransportLayerWba::GetVersion() const
    {
        Version version{};
        really_assert(hci_read_local_version_information(&version.hciVersion, &version.hciSubversion, &version.lmpVersion, &version.companyIdentifier, &version.lmpSubversion) == BLE_STATUS_SUCCESS);

        std::array<uint32_t, 2> stackVersion{};
        std::array<uint32_t, 1> options{};
        std::array<uint32_t, 3> debugInfo{};
        really_assert(aci_get_information(stackVersion.data(), options.data(), debugInfo.data()) == BLE_STATUS_SUCCESS);
        version.firmwareBuildNumber = static_cast<uint16_t>(stackVersion[1] >> 16);

        return version;
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
            if (!eventFlowPaused)
            {
                eventFlowPaused = true;
                EventFlowPaused();
            }

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
            EventFlowResumed();
        }

        SVCCTL_UserEvtRx(packet.data());
    }
}
