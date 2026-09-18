#ifndef HAL_ST_GATT_CLIENT_ST_HPP
#define HAL_ST_GATT_CLIENT_ST_HPP

#include "ble/ble.h"
#include "hal_st/middlewares/ble_middleware/HciEventObserver.hpp"
#include "infra/stream/ByteInputStream.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedObjectAllocatorFixedSize.hpp"
#include "infra/util/WithStorage.hpp"
#include "services/ble/GattClient.hpp"
#include "services/ble/GattClientConnection.hpp"
#include "services/ble/GattClientLongOperations.hpp"

namespace hal
{
    class GattClientConnectionSt
        : public services::GattClientConnection
        , public services::GattClientLongOperations
    {
    public:
        explicit GattClientConnectionSt(uint16_t connectionHandle);

        uint16_t ConnectionHandle() const;

        // Implementation of services::GattClientConnection
        uint16_t EffectiveMaxAttMtuSize() const override;
        services::GattRequestStatus ExchangeMtu(const infra::Function<void(services::GattResult)>& onDone) override;

        using services::GattClientConnection::DiscoverCharacteristics;
        using services::GattClientConnection::DiscoverDescriptors;
        using services::GattClientConnection::DiscoverIncludedServices;

        services::GattRequestStatus DiscoverServices(const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DiscoverCharacteristics(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DiscoverDescriptors(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DiscoverIncludedServices(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone) override;

        services::GattRequestStatus Read(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone) override;
        services::GattRequestStatus Write(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus WriteWithoutResponse(services::AttAttribute::Handle handle, infra::ConstByteRange data) override;

        services::GattRequestStatus ReadBlob(services::AttAttribute::Handle handle, uint16_t offset, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone) override;
        services::GattRequestStatus PrepareWrite(services::AttAttribute::Handle handle, uint16_t offset, infra::ConstByteRange data, const infra::Function<void(services::GattResult, uint16_t, infra::ConstByteRange)>& onDone) override;
        services::GattRequestStatus ExecuteWrite(services::GattExecuteWriteFlag flag, const infra::Function<void(services::GattResult)>& onDone) override;

        services::GattRequestStatus EnableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DisableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus EnableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DisableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;

        // Implementation of services::GattClientLongOperations
        services::GattRequestStatus ReadLong(services::AttAttribute::Handle handle, infra::BoundedVector<uint8_t>& value, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone) override;
        services::GattRequestStatus WriteLong(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone) override;

        // Called by GattClientSt for the events addressed to this connection
        void MtuExchanged(uint16_t mtu);
        void ProcedureComplete(uint8_t errorCode);
        void ReadResponse(infra::ConstByteRange data);
        void ReadBlobResponse(infra::ConstByteRange data);
        void ServicesDiscovered(infra::DataInputStream& stream, bool isUuid16);
        void CharacteristicsOrIncludedServicesDiscovered(infra::DataInputStream& stream, uint8_t pairLength);
        void DescriptorsDiscovered(infra::DataInputStream& stream, bool isUuid16);
        void NotificationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data);
        void IndicationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data);
        void Released();

    private:
        // The ST stack runs one ATT procedure per link at a time, so one operation is in flight at
        // most and its kind says how the responses that follow are to be read.
        enum class Operation : uint8_t
        {
            none,
            exchangeMtu,
            discoverServices,
            discoverCharacteristics,
            discoverDescriptors,
            discoverIncludedServices,
            read,
            readLong,
            write,
            writeLong,
            writeDescriptor
        };

        // The command is issued only once the connection is known to be free, so a refused request
        // does not put a second ATT procedure on the link before reporting busy.
        template<class IssueCommand>
        services::GattRequestStatus Start(Operation operation, IssueCommand issue, const infra::Function<void(services::GattResult)>& onDone)
        {
            if (this->operation != Operation::none)
                return services::GattRequestStatus::busy;

            return Started(operation, issue(), onDone);
        }

        services::GattRequestStatus Started(Operation operation, tBleStatus status, const infra::Function<void(services::GattResult)>& onDone);
        services::GattRequestStatus WriteClientCharacteristicConfiguration(services::AttAttribute::Handle valueHandle, services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue value, const infra::Function<void(services::GattResult)>& onDone);
        static void ReadUuid(infra::DataInputStream& stream, bool isUuid16, services::AttAttribute::Uuid& type);

    private:
        uint16_t connectionHandle;
        uint16_t maxAttMtu = services::attDefaultMaxMtuSize;

        Operation operation = Operation::none;
        infra::BoundedVector<uint8_t>* readLongValue = nullptr;

        infra::AutoResetFunction<void(services::GattResult)> onOperationDone;
        infra::AutoResetFunction<void(services::GattResult, infra::ConstByteRange)> onReadDone;

        services::GattIndicationFanOut indicationFanOut;
    };

    template<std::size_t MaxConnections>
    struct GattClientStStorage
    {
        typename infra::SharedObjectAllocatorFixedSize<GattClientConnectionSt, void(uint16_t)>::template WithStorage<MaxConnections> connections;
        typename infra::BoundedVector<infra::SharedPtr<GattClientConnectionSt>>::template WithMaxSize<MaxConnections> established;
    };

    class GattClientSt
        : public services::GattClient
        , public hal::HciEventSink
    {
    public:
        template<std::size_t MaxConnections>
        using WithMaxConnections = infra::WithStorage<GattClientSt, GattClientStStorage<MaxConnections>>;

        template<std::size_t MaxConnections>
        GattClientSt(GattClientStStorage<MaxConnections>& storage, hal::HciEventSource& hciEventSource)
            : GattClientSt(storage.connections, storage.established, MaxConnections, hciEventSource)
        {}

        // Implementation of services::GattClient
        std::size_t MaxNumberOfConnections() const override;
        std::size_t NumberOfConnections() const override;

        // Implementation of hal::HciEventSink
        void HciEvent(hci_event_pckt& event) override;

    protected:
        GattClientSt(infra::SharedObjectAllocator<GattClientConnectionSt, void(uint16_t)>& connections,
            infra::BoundedVector<infra::SharedPtr<GattClientConnectionSt>>& established,
            std::size_t maxNumberOfConnections,
            hal::HciEventSource& hciEventSource);

        virtual void HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event);
        virtual void HandleHciLeMetaEvent(const evt_le_meta_event& metaEvent);
        virtual void HandleHciVendorSpecificDebugEvent(const evt_blecore_aci& event);

        virtual void HandleConnectionComplete(uint8_t status, uint16_t connectionHandle);

        GattClientConnectionSt* ConnectionOf(uint16_t connectionHandle) const;

    private:
        infra::SharedObjectAllocator<GattClientConnectionSt, void(uint16_t)>& connections;
        infra::BoundedVector<infra::SharedPtr<GattClientConnectionSt>>& established;
        std::size_t maxNumberOfConnections;
    };
}

#endif
