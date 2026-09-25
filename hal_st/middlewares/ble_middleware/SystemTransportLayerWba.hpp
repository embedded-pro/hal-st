#pragma once

#include "hal_st/middlewares/ble_middleware/BleNvmWba.hpp"
#include "hal_st/middlewares/ble_middleware/BlePlatformWba.hpp"
#include "hal_st/middlewares/ble_middleware/HciEventObserver.hpp"
#include "hal_st/middlewares/ble_middleware/LinkLayerPlatformWba.hpp"
#include "infra/util/BoundedDeque.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/WithStorage.hpp"
#include "services/util/ConfigurationStore.hpp"
#include <array>
#include <cstdint>

extern "C"
{
#include "ble_bufsize.h"
#include "ble_common.h"
}

namespace hal
{
    class SystemTransportLayerWba
        : public infra::InterfaceConnector<SystemTransportLayerWba>
        , public HciEventSource
    {
    public:
        static constexpr uint8_t numberOfAttributeRecords = 0x44;
        static constexpr uint8_t numberOfAttributeServices = 0x08;
        static constexpr uint16_t attributeValueArraySize = 0x540;

        // BLE middleware supports an ATT MTU of 512; the HCI buffer limits this port to 251.
        static constexpr uint16_t maxAttMtuSizeLimit = 251;

        // ST's CFG_BLEPLAT_NVM_MAX_SIZE; the ConfigurationStore entry passed to the constructor must match.
        static constexpr std::size_t bondBlobSize = 504 * sizeof(uint32_t);

        // The stack allocates its memory blocks per link, so its buffer size follows the number of
        // links it is initialised with and has to be known where the buffer is defined.
        template<uint8_t NumberOfLinks>
        struct Storage
        {
            static constexpr std::size_t mblockCount = BLE_MBLOCKS_CALC(BLE_DEFAULT_PREP_WRITE_LIST_SIZE, maxAttMtuSizeLimit, NumberOfLinks) + 0x15;
            static constexpr std::size_t stackBufferSize = BLE_TOTAL_BUFFER_SIZE(NumberOfLinks, mblockCount);
            static constexpr std::size_t gattBufferSize = BLE_TOTAL_BUFFER_SIZE_GATT(numberOfAttributeRecords, numberOfAttributeServices, attributeValueArraySize);

            std::array<uint32_t, DIVC(stackBufferSize, 4)> stack{};
            std::array<uint32_t, DIVC(gattBufferSize, 4)> gatt{};
            std::array<BlePlatformWba::TimerSlot, BlePlatformWba::TimersForLinks(NumberOfLinks)> timers;
            std::array<uint32_t, bondBlobSize / sizeof(uint32_t)> nvm{};
        };

        template<uint8_t NumberOfLinks>
        using WithLinks = infra::WithStorage<SystemTransportLayerWba, Storage<NumberOfLinks>>;

        struct HardwareDependencies
        {
            LinkLayerPlatformWba::RandomDataGeneratorCreator& randomDataGenerator;
            BlePlatformWba::AesCreator& aes;
            BlePlatformWba::PkaCreator& pka;
        };

        struct Config
        {
            constexpr Config()
            {}

            uint16_t maxAttMtuSize = maxAttMtuSizeLimit;
            LinkLayerPlatformWba::Config linkLayer;
        };

        template<uint8_t NumberOfLinks>
        SystemTransportLayerWba(Storage<NumberOfLinks>& storage, services::ConfigurationStoreAccess<infra::ByteRange> bondBlob, const HardwareDependencies& hardware, const Config& config = Config())
            : SystemTransportLayerWba(infra::MakeRange(storage.stack), infra::MakeRange(storage.gatt), infra::MakeRange(storage.timers), infra::MakeRange(storage.nvm), bondBlob, hardware, StackConfig{ config, NumberOfLinks, Storage<NumberOfLinks>::mblockCount })
        {}

        // Implementation of HciEventSource
        void HciEventHandler(hci_event_pckt& event) override;

        // Observers may call into the stack, which it forbids while BleStack_Process runs, so events
        // are handed to them afterwards from the event dispatcher.
        bool QueueEvent(infra::ConstByteRange packet);

    private:
        struct StackConfig
            : Config
        {
            uint8_t numberOfLinks;
            uint16_t mblockCount;
        };

        SystemTransportLayerWba(infra::MemoryRange<uint32_t> stackBuffer, infra::MemoryRange<uint32_t> gattBuffer, infra::MemoryRange<BlePlatformWba::TimerSlot> timers, infra::MemoryRange<uint32_t> nvmRecords, services::ConfigurationStoreAccess<infra::ByteRange> bondBlob, const HardwareDependencies& hardware, const StackConfig& config);

        void ProcessQueuedEvent();

    private:
        LinkLayerPlatformWba linkLayerPlatform;
        BlePlatformWba blePlatform;
        BleNvmWba nvm;

        static constexpr std::size_t maxEventPacketSize = 3 + 255;
        static constexpr std::size_t maxQueuedEvents = 4;

        infra::BoundedDeque<std::array<uint8_t, maxEventPacketSize>>::WithMaxSize<maxQueuedEvents> events;
        bool eventFlowPaused = false;
    };
}
