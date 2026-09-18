#ifndef HAL_ST_TRACING_GATT_CLIENT_ST_HPP
#define HAL_ST_TRACING_GATT_CLIENT_ST_HPP

#include "hal_st/middlewares/ble_middleware/GattClientSt.hpp"
#include "services/tracer/Tracer.hpp"

namespace hal
{
    // Traces the connections a GATT client holds. The operations on a connection are traced by
    // TracingGattClientConnection, which an application puts in front of the connection it is
    // handed, the way the claiming and retrying decorators of services/ble are applied.
    class TracingGattClientSt
        : public GattClientSt
    {
    public:
        template<std::size_t MaxConnections>
        using WithMaxConnections = infra::WithStorage<TracingGattClientSt, GattClientStStorage<MaxConnections>>;

        template<std::size_t MaxConnections>
        TracingGattClientSt(GattClientStStorage<MaxConnections>& storage, hal::HciEventSource& hciEventSource, services::Tracer& tracer)
            : GattClientSt(storage, hciEventSource)
            , tracer(tracer)
        {}

    protected:
        void HandleConnectionComplete(uint8_t status, uint16_t connectionHandle) override;
        void HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event) override;

    private:
        services::Tracer& tracer;
    };

    // GattClientConnectionSt also serves the long operations, so a decorator that only forwarded
    // GattClientConnection would take ReadLong and WriteLong away from whoever it is put in front of.
    class TracingGattClientConnection
        : public services::GattClientConnectionDecorator
        , public services::GattClientLongOperations
    {
    public:
        TracingGattClientConnection(services::GattClientConnection& connection, services::GattClientLongOperations& longOperations, services::Tracer& tracer);

        using services::GattClientConnectionDecorator::DiscoverCharacteristics;
        using services::GattClientConnectionDecorator::DiscoverDescriptors;
        using services::GattClientConnectionDecorator::DiscoverIncludedServices;

        // Implementation of services::GattClientConnection
        services::GattRequestStatus ExchangeMtu(const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DiscoverServices(const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DiscoverCharacteristics(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DiscoverDescriptors(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DiscoverIncludedServices(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus Read(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone) override;
        services::GattRequestStatus Write(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus WriteWithoutResponse(services::AttAttribute::Handle handle, infra::ConstByteRange data) override;
        services::GattRequestStatus EnableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DisableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus EnableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;
        services::GattRequestStatus DisableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone) override;

        // Implementation of services::GattClientLongOperations
        services::GattRequestStatus ReadLong(services::AttAttribute::Handle handle, infra::BoundedVector<uint8_t>& value, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone) override;
        services::GattRequestStatus WriteLong(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone) override;

        // Implementation of services::GattClientConnectionObserver
        void ServiceDiscovered(const services::GattService& service) override;
        void IncludedServiceDiscovered(const services::GattIncludedService& includedService) override;
        void CharacteristicDiscovered(const services::GattCharacteristic& characteristic) override;
        void DescriptorDiscovered(const services::GattDescriptor& descriptor) override;
        void MtuChanged(uint16_t mtu) override;

        // Implementation of services::GattClientUpdateObserver
        void NotificationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data) override;
        void IndicationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void()>& onDone) override;

    private:
        services::GattRequestStatus TraceRequest(infra::BoundedConstString procedure, services::GattRequestStatus status) const;

    private:
        services::GattClientLongOperations& longOperations;
        services::Tracer& tracer;
    };
}

namespace infra
{
    TextOutputStream& operator<<(TextOutputStream& stream, const services::GattRequestStatus& status);
    TextOutputStream& operator<<(TextOutputStream& stream, const services::GattResult& result);
}

#endif
