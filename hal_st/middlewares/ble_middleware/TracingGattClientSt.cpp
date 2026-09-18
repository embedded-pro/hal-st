#include "hal_st/middlewares/ble_middleware/TracingGattClientSt.hpp"

namespace infra
{
    TextOutputStream& operator<<(TextOutputStream& stream, const services::GattRequestStatus& status)
    {
        switch (status)
        {
            case services::GattRequestStatus::accepted:
                return stream << "accepted";
            case services::GattRequestStatus::invalidState:
                return stream << "invalidState";
            case services::GattRequestStatus::invalidParameter:
                return stream << "invalidParameter";
            case services::GattRequestStatus::busy:
                return stream << "busy";
            default:
                return stream << "notSupported";
        }
    }

    TextOutputStream& operator<<(TextOutputStream& stream, const services::GattResult& result)
    {
        switch (result)
        {
            case services::GattResult::success:
                return stream << "success";
            case services::GattResult::disconnected:
                return stream << "disconnected";
            case services::GattResult::timeout:
                return stream << "timeout";
            default:
                return stream << "error 0x" << infra::hex << infra::enum_cast(result);
        }
    }
}

namespace hal
{
    void TracingGattClientSt::HandleConnectionComplete(uint8_t status, uint16_t connectionHandle)
    {
        GattClientSt::HandleConnectionComplete(status, connectionHandle);

        if (status == BLE_STATUS_SUCCESS)
            tracer.Trace() << "TracingGattClientSt::ConnectionEstablished, handle: 0x" << infra::hex << connectionHandle << ", connections: " << NumberOfConnections() << "/" << MaxNumberOfConnections();
    }

    void TracingGattClientSt::HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event)
    {
        GattClientSt::HandleHciDisconnectEvent(event);

        tracer.Trace() << "TracingGattClientSt::ConnectionReleased, handle: 0x" << infra::hex << event.Connection_Handle << ", connections: " << NumberOfConnections() << "/" << MaxNumberOfConnections();
    }

    TracingGattClientConnection::TracingGattClientConnection(services::GattClientConnection& connection, services::GattClientLongOperations& longOperations, services::Tracer& tracer)
        : services::GattClientConnectionDecorator(connection)
        , longOperations(longOperations)
        , tracer(tracer)
    {}

    services::GattRequestStatus TracingGattClientConnection::TraceRequest(infra::BoundedConstString procedure, services::GattRequestStatus status) const
    {
        tracer.Trace() << "TracingGattClientConnection::" << procedure << " -> " << status;
        return status;
    }

    services::GattRequestStatus TracingGattClientConnection::ExchangeMtu(const infra::Function<void(services::GattResult)>& onDone)
    {
        return TraceRequest("ExchangeMtu", services::GattClientConnectionDecorator::ExchangeMtu(onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::DiscoverServices(const infra::Function<void(services::GattResult)>& onDone)
    {
        return TraceRequest("DiscoverServices", services::GattClientConnectionDecorator::DiscoverServices(onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::DiscoverCharacteristics(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::DiscoverCharacteristics [0x" << infra::hex << handle << ", 0x" << infra::hex << endHandle << "]";
        return TraceRequest("DiscoverCharacteristics", services::GattClientConnectionDecorator::DiscoverCharacteristics(handle, endHandle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::DiscoverDescriptors(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::DiscoverDescriptors [0x" << infra::hex << handle << ", 0x" << infra::hex << endHandle << "]";
        return TraceRequest("DiscoverDescriptors", services::GattClientConnectionDecorator::DiscoverDescriptors(handle, endHandle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::DiscoverIncludedServices(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::DiscoverIncludedServices [0x" << infra::hex << handle << ", 0x" << infra::hex << endHandle << "]";
        return TraceRequest("DiscoverIncludedServices", services::GattClientConnectionDecorator::DiscoverIncludedServices(handle, endHandle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::Read(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::Read [0x" << infra::hex << handle << "]";
        return TraceRequest("Read", services::GattClientConnectionDecorator::Read(handle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::Write(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::Write [0x" << infra::hex << handle << "] 0x" << infra::AsHex(data);
        return TraceRequest("Write", services::GattClientConnectionDecorator::Write(handle, data, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::WriteWithoutResponse(services::AttAttribute::Handle handle, infra::ConstByteRange data)
    {
        tracer.Trace() << "TracingGattClientConnection::WriteWithoutResponse [0x" << infra::hex << handle << "] 0x" << infra::AsHex(data);
        return TraceRequest("WriteWithoutResponse", services::GattClientConnectionDecorator::WriteWithoutResponse(handle, data));
    }

    services::GattRequestStatus TracingGattClientConnection::EnableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::EnableNotification [0x" << infra::hex << handle << "]";
        return TraceRequest("EnableNotification", services::GattClientConnectionDecorator::EnableNotification(handle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::DisableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::DisableNotification [0x" << infra::hex << handle << "]";
        return TraceRequest("DisableNotification", services::GattClientConnectionDecorator::DisableNotification(handle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::EnableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::EnableIndication [0x" << infra::hex << handle << "]";
        return TraceRequest("EnableIndication", services::GattClientConnectionDecorator::EnableIndication(handle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::DisableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::DisableIndication [0x" << infra::hex << handle << "]";
        return TraceRequest("DisableIndication", services::GattClientConnectionDecorator::DisableIndication(handle, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::ReadLong(services::AttAttribute::Handle handle, infra::BoundedVector<uint8_t>& value, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::ReadLong [0x" << infra::hex << handle << "] into " << value.max_size() << " bytes";
        return TraceRequest("ReadLong", longOperations.ReadLong(handle, value, onDone));
    }

    services::GattRequestStatus TracingGattClientConnection::WriteLong(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::WriteLong [0x" << infra::hex << handle << "] " << data.size() << " bytes";
        return TraceRequest("WriteLong", longOperations.WriteLong(handle, data, onDone));
    }

    void TracingGattClientConnection::ServiceDiscovered(const services::GattService& service)
    {
        tracer.Trace() << "TracingGattClientConnection::ServiceDiscovered " << service.Type() << " [0x" << infra::hex << service.Handle() << ", 0x" << infra::hex << service.EndHandle() << "]";
        services::GattClientConnectionDecorator::ServiceDiscovered(service);
    }

    void TracingGattClientConnection::IncludedServiceDiscovered(const services::GattIncludedService& includedService)
    {
        tracer.Trace() << "TracingGattClientConnection::IncludedServiceDiscovered " << includedService.Type() << " [0x" << infra::hex << includedService.ServiceHandle() << ", 0x" << infra::hex << includedService.ServiceEndHandle() << "]";
        services::GattClientConnectionDecorator::IncludedServiceDiscovered(includedService);
    }

    void TracingGattClientConnection::CharacteristicDiscovered(const services::GattCharacteristic& characteristic)
    {
        tracer.Trace() << "TracingGattClientConnection::CharacteristicDiscovered " << characteristic.Type() << " [0x" << infra::hex << characteristic.Handle() << "] value [0x" << infra::hex << characteristic.ValueHandle() << "] " << characteristic.Properties();
        services::GattClientConnectionDecorator::CharacteristicDiscovered(characteristic);
    }

    void TracingGattClientConnection::DescriptorDiscovered(const services::GattDescriptor& descriptor)
    {
        tracer.Trace() << "TracingGattClientConnection::DescriptorDiscovered " << descriptor.Type() << " [0x" << infra::hex << descriptor.Handle() << "]";
        services::GattClientConnectionDecorator::DescriptorDiscovered(descriptor);
    }

    void TracingGattClientConnection::MtuChanged(uint16_t mtu)
    {
        tracer.Trace() << "TracingGattClientConnection::MtuChanged " << mtu;
        services::GattClientConnectionDecorator::MtuChanged(mtu);
    }

    void TracingGattClientConnection::NotificationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data)
    {
        tracer.Trace() << "TracingGattClientConnection::NotificationReceived [0x" << infra::hex << handle << "] 0x" << infra::AsHex(data);
        services::GattClientConnectionDecorator::NotificationReceived(handle, data);
    }

    void TracingGattClientConnection::IndicationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void()>& onDone)
    {
        tracer.Trace() << "TracingGattClientConnection::IndicationReceived [0x" << infra::hex << handle << "] 0x" << infra::AsHex(data);
        services::GattClientConnectionDecorator::IndicationReceived(handle, data, onDone);
    }
}
