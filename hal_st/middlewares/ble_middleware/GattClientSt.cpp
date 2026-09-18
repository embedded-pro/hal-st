#include "hal_st/middlewares/ble_middleware/GattClientSt.hpp"
#include "infra/event/EventDispatcherWithWeakPtr.hpp"
#include "infra/stream/InputStream.hpp"
#include "infra/util/Endian.hpp"
#include <algorithm>

extern "C"
{
#include "auto/ble_gatt_aci.h"
#include "ble/ble.h"
}

namespace
{
    constexpr uint16_t invalidConnection = 0xffff;

    // The Client Characteristic Configuration descriptor of a characteristic follows its value.
    // Bluetooth Core Specification, Volume 3, Part G, section 3.3.3
    constexpr services::AttAttribute::Handle clientCharacteristicConfigurationOffset = 1;

    services::GattRequestStatus RequestStatusOf(tBleStatus status)
    {
        if (status == BLE_STATUS_SUCCESS)
            return services::GattRequestStatus::accepted;
        if (status == BLE_STATUS_INSUFFICIENT_RESOURCES || status == BLE_STATUS_BUSY)
            return services::GattRequestStatus::busy;
        if (status == BLE_STATUS_INVALID_PARAMS)
            return services::GattRequestStatus::invalidParameter;

        return services::GattRequestStatus::invalidState;
    }
}

namespace hal
{
    GattClientConnectionSt::GattClientConnectionSt(uint16_t connectionHandle)
        : connectionHandle(connectionHandle)
    {}

    uint16_t GattClientConnectionSt::ConnectionHandle() const
    {
        return connectionHandle;
    }

    uint16_t GattClientConnectionSt::EffectiveMaxAttMtuSize() const
    {
        return maxAttMtu;
    }

    services::GattRequestStatus GattClientConnectionSt::ExchangeMtu(const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::exchangeMtu, [this]
            {
                return aci_gatt_exchange_config(connectionHandle);
            },
            onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::DiscoverServices(const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::discoverServices, [this]
            {
                return aci_gatt_disc_all_primary_services(connectionHandle);
            },
            onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::DiscoverCharacteristics(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::discoverCharacteristics, [this, handle, endHandle]
            {
                return aci_gatt_disc_all_char_of_service(connectionHandle, handle, endHandle);
            },
            onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::DiscoverDescriptors(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::discoverDescriptors, [this, handle, endHandle]
            {
                return aci_gatt_disc_all_char_desc(connectionHandle, handle, endHandle);
            },
            onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::DiscoverIncludedServices(services::AttAttribute::Handle handle, services::AttAttribute::Handle endHandle, const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::discoverIncludedServices, [this, handle, endHandle]
            {
                return aci_gatt_find_included_services(connectionHandle, handle, endHandle);
            },
            onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::Read(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone)
    {
        if (operation != Operation::none)
            return services::GattRequestStatus::busy;

        auto status = RequestStatusOf(aci_gatt_read_char_value(connectionHandle, handle));

        if (status != services::GattRequestStatus::accepted)
            return status;

        operation = Operation::read;
        onReadDone = onDone;

        return services::GattRequestStatus::accepted;
    }

    services::GattRequestStatus GattClientConnectionSt::Write(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::write, [this, handle, data]
            {
                return aci_gatt_write_char_value(connectionHandle, handle, data.size(), data.cbegin());
            },
            onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::WriteWithoutResponse(services::AttAttribute::Handle handle, infra::ConstByteRange data)
    {
        return RequestStatusOf(aci_gatt_write_without_resp(connectionHandle, handle, data.size(), data.cbegin()));
    }

    services::GattRequestStatus GattClientConnectionSt::ReadBlob(services::AttAttribute::Handle handle, uint16_t offset, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone)
    {
        // ACI_GATT_READ_LONG_CHAR_VALUE runs the whole Read Long procedure from an offset rather
        // than sending one Read Blob Request, and the value it collects needs somewhere to go.
        // ReadLong is that operation.
        return services::GattRequestStatus::notSupported;
    }

    services::GattRequestStatus GattClientConnectionSt::PrepareWrite(services::AttAttribute::Handle handle, uint16_t offset, infra::ConstByteRange data, const infra::Function<void(services::GattResult, uint16_t, infra::ConstByteRange)>& onDone)
    {
        // The ST ACI exposes no Prepare Write Request of its own; it only runs complete procedures
        // that queue and execute internally. WriteLong is that operation.
        return services::GattRequestStatus::notSupported;
    }

    services::GattRequestStatus GattClientConnectionSt::ExecuteWrite(services::GattExecuteWriteFlag flag, const infra::Function<void(services::GattResult)>& onDone)
    {
        return services::GattRequestStatus::notSupported;
    }

    services::GattRequestStatus GattClientConnectionSt::EnableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        return WriteClientCharacteristicConfiguration(handle, services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue::enableNotification, onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::DisableNotification(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        return WriteClientCharacteristicConfiguration(handle, services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue::disable, onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::EnableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        return WriteClientCharacteristicConfiguration(handle, services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue::enableIndication, onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::DisableIndication(services::AttAttribute::Handle handle, const infra::Function<void(services::GattResult)>& onDone)
    {
        return WriteClientCharacteristicConfiguration(handle, services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue::disable, onDone);
    }

    services::GattRequestStatus GattClientConnectionSt::ReadLong(services::AttAttribute::Handle handle, infra::BoundedVector<uint8_t>& value, const infra::Function<void(services::GattResult, infra::ConstByteRange)>& onDone)
    {
        if (operation != Operation::none)
            return services::GattRequestStatus::busy;

        constexpr uint16_t fromTheStart = 0;
        auto status = RequestStatusOf(aci_gatt_read_long_char_value(connectionHandle, handle, fromTheStart));

        if (status != services::GattRequestStatus::accepted)
            return status;

        value.clear();

        operation = Operation::readLong;
        readLongValue = &value;
        onReadDone = onDone;

        return services::GattRequestStatus::accepted;
    }

    services::GattRequestStatus GattClientConnectionSt::WriteLong(services::AttAttribute::Handle handle, infra::ConstByteRange data, const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::writeLong, [this, handle, data]
            {
                constexpr uint16_t fromTheStart = 0;
                return aci_gatt_write_long_char_value(connectionHandle, handle, fromTheStart, data.size(), data.cbegin());
            },
            onDone);
    }

    void GattClientConnectionSt::MtuExchanged(uint16_t mtu)
    {
        maxAttMtu = mtu;

        infra::Subject<services::GattClientConnectionObserver>::NotifyObservers([mtu](auto& observer)
            {
                observer.MtuChanged(mtu);
            });
    }

    void GattClientConnectionSt::ProcedureComplete(uint8_t errorCode)
    {
        auto completed = std::exchange(operation, Operation::none);
        auto result = services::GattResultFromAttErrorCode(errorCode);

        if (completed == Operation::readLong)
        {
            auto value = readLongValue;
            readLongValue = nullptr;

            if (onReadDone)
                onReadDone(result, value != nullptr ? infra::ConstByteRange(infra::MakeRange(*value)) : infra::ConstByteRange());
        }
        else if (completed == Operation::read)
        {
            // A value arrives in its own response; reaching here with the callback still in place
            // means none did.
            if (onReadDone)
                onReadDone(result, infra::ConstByteRange());
        }
        else if (onOperationDone)
            onOperationDone(result);
    }

    void GattClientConnectionSt::ReadResponse(infra::ConstByteRange data)
    {
        if (operation != Operation::read)
            return;

        operation = Operation::none;

        if (onReadDone)
            onReadDone(services::GattResult::success, data);
    }

    void GattClientConnectionSt::ReadBlobResponse(infra::ConstByteRange data)
    {
        if (operation != Operation::readLong || readLongValue == nullptr)
            return;

        if (readLongValue->max_size() - readLongValue->size() < data.size())
        {
            readLongValue->insert(readLongValue->end(), data.begin(), data.begin() + (readLongValue->max_size() - readLongValue->size()));
            operation = Operation::none;
            readLongValue = nullptr;

            if (onReadDone)
                onReadDone(services::GattResult::insufficientResources, infra::ConstByteRange());

            return;
        }

        readLongValue->insert(readLongValue->end(), data.begin(), data.end());
    }

    void GattClientConnectionSt::ServicesDiscovered(infra::DataInputStream& stream, bool isUuid16)
    {
        while (!stream.Empty())
        {
            services::AttAttribute::Uuid type;
            services::AttAttribute::Handle handle = 0;
            services::AttAttribute::Handle endHandle = 0;

            stream >> handle >> endHandle;
            ReadUuid(stream, isUuid16, type);

            really_assert(!stream.Failed());

            services::GattService service{ type, handle, endHandle };

            infra::Subject<services::GattClientConnectionObserver>::NotifyObservers([&service](auto& observer)
                {
                    observer.ServiceDiscovered(service);
                });
        }
    }

    void GattClientConnectionSt::CharacteristicsOrIncludedServicesDiscovered(infra::DataInputStream& stream, uint8_t pairLength)
    {
        // Both procedures answer with Read By Type responses; which one is in flight says how a
        // pair is read. A characteristic pair carries the declaration handle, the properties, the
        // value handle and the type; an include declaration carries its own handle, the included
        // service's handle range and, for a 16 bit service only, its type.
        // Bluetooth Core Specification, Volume 3, Part G, sections 3.2 and 3.3.1
        constexpr uint8_t characteristicPairLengthUuid16 = 7;
        constexpr uint8_t includedServicePairLengthWithUuid = 8;

        if (operation == Operation::discoverIncludedServices)
        {
            while (!stream.Empty())
            {
                services::AttAttribute::Uuid type;
                services::AttAttribute::Handle handle = 0;
                services::AttAttribute::Handle serviceHandle = 0;
                services::AttAttribute::Handle serviceEndHandle = 0;

                stream >> handle >> serviceHandle >> serviceEndHandle;

                if (pairLength == includedServicePairLengthWithUuid)
                    ReadUuid(stream, true, type);

                really_assert(!stream.Failed());

                services::GattIncludedService includedService{ type, handle, serviceHandle, serviceEndHandle };

                infra::Subject<services::GattClientConnectionObserver>::NotifyObservers([&includedService](auto& observer)
                    {
                        observer.IncludedServiceDiscovered(includedService);
                    });
            }

            return;
        }

        while (!stream.Empty())
        {
            services::AttAttribute::Uuid type;
            services::AttAttribute::Handle handle = 0;
            services::AttAttribute::Handle valueHandle = 0;
            services::GattCharacteristic::PropertyFlags properties = services::GattCharacteristic::PropertyFlags::none;

            stream >> handle >> properties >> valueHandle;
            ReadUuid(stream, pairLength == characteristicPairLengthUuid16, type);

            really_assert(!stream.Failed());

            services::GattCharacteristic characteristic{ type, handle, valueHandle, properties };

            infra::Subject<services::GattClientConnectionObserver>::NotifyObservers([&characteristic](auto& observer)
                {
                    observer.CharacteristicDiscovered(characteristic);
                });
        }
    }

    void GattClientConnectionSt::DescriptorsDiscovered(infra::DataInputStream& stream, bool isUuid16)
    {
        while (!stream.Empty())
        {
            services::AttAttribute::Uuid type;
            services::AttAttribute::Handle handle = 0;

            stream >> handle;
            ReadUuid(stream, isUuid16, type);

            really_assert(!stream.Failed());

            services::GattDescriptor descriptor{ type, handle };

            infra::Subject<services::GattClientConnectionObserver>::NotifyObservers([&descriptor](auto& observer)
                {
                    observer.DescriptorDiscovered(descriptor);
                });
        }
    }

    void GattClientConnectionSt::NotificationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data)
    {
        infra::Subject<services::GattClientUpdateObserver>::NotifyObservers([handle, &data](auto& observer)
            {
                observer.NotificationReceived(handle, data);
            });
    }

    void GattClientConnectionSt::IndicationReceived(services::AttAttribute::Handle handle, infra::ConstByteRange data)
    {
        indicationFanOut.Deliver(static_cast<infra::Subject<services::GattClientUpdateObserver>&>(*this), handle, data, [this]()
            {
                aci_gatt_confirm_indication(connectionHandle);
            });
    }

    void GattClientConnectionSt::Released()
    {
        auto completed = std::exchange(operation, Operation::none);
        readLongValue = nullptr;
        connectionHandle = invalidConnection;

        if (completed == Operation::read || completed == Operation::readLong)
        {
            if (onReadDone)
                onReadDone(services::GattResult::disconnected, infra::ConstByteRange());
        }
        else if (onOperationDone)
            onOperationDone(services::GattResult::disconnected);
    }

    services::GattRequestStatus GattClientConnectionSt::Started(Operation operation, tBleStatus status, const infra::Function<void(services::GattResult)>& onDone)
    {
        auto requestStatus = RequestStatusOf(status);

        if (requestStatus != services::GattRequestStatus::accepted)
            return requestStatus;

        this->operation = operation;
        onOperationDone = onDone;

        return services::GattRequestStatus::accepted;
    }

    services::GattRequestStatus GattClientConnectionSt::WriteClientCharacteristicConfiguration(services::AttAttribute::Handle valueHandle, services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue value, const infra::Function<void(services::GattResult)>& onDone)
    {
        return Start(Operation::writeDescriptor, [this, valueHandle, value]
            {
                auto configuration = infra::ToLittleEndian(static_cast<uint16_t>(value));
                return aci_gatt_write_char_desc(connectionHandle, valueHandle + clientCharacteristicConfigurationOffset, sizeof(configuration), reinterpret_cast<const uint8_t*>(&configuration));
            },
            onDone);
    }

    void GattClientConnectionSt::ReadUuid(infra::DataInputStream& stream, bool isUuid16, services::AttAttribute::Uuid& type)
    {
        if (isUuid16)
            stream >> type.emplace<services::AttAttribute::Uuid16>();
        else
            stream >> type.emplace<services::AttAttribute::Uuid128>();
    }

    GattClientSt::GattClientSt(infra::SharedObjectAllocator<GattClientConnectionSt, void(uint16_t)>& connections,
        infra::BoundedVector<infra::SharedPtr<GattClientConnectionSt>>& established,
        std::size_t maxNumberOfConnections,
        hal::HciEventSource& hciEventSource)
        : hal::HciEventSink(hciEventSource)
        , connections(connections)
        , established(established)
        , maxNumberOfConnections(maxNumberOfConnections)
    {}

    std::size_t GattClientSt::MaxNumberOfConnections() const
    {
        return maxNumberOfConnections;
    }

    std::size_t GattClientSt::NumberOfConnections() const
    {
        return established.size();
    }

    void GattClientSt::HciEvent(hci_event_pckt& event)
    {
        switch (event.evt)
        {
            case HCI_DISCONNECTION_COMPLETE_EVT_CODE:
                HandleHciDisconnectEvent(*reinterpret_cast<const hci_disconnection_complete_event_rp0*>(event.data));
                break;
            case HCI_LE_META_EVT_CODE:
                HandleHciLeMetaEvent(*reinterpret_cast<const evt_le_meta_event*>(event.data));
                break;
            case HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE:
                HandleHciVendorSpecificDebugEvent(*reinterpret_cast<const evt_blecore_aci*>(event.data));
                break;
            default:
                break;
        }
    }

    void GattClientSt::HandleHciLeMetaEvent(const evt_le_meta_event& metaEvent)
    {
        switch (metaEvent.subevent)
        {
            case HCI_LE_ENHANCED_CONNECTION_COMPLETE_SUBEVT_CODE:
            {
                const auto& event = *reinterpret_cast<const hci_le_enhanced_connection_complete_event_rp0*>(metaEvent.data);
                HandleConnectionComplete(event.Status, event.Connection_Handle);
                break;
            }
            case HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE:
            {
                const auto& event = *reinterpret_cast<const hci_le_connection_complete_event_rp0*>(metaEvent.data);
                HandleConnectionComplete(event.Status, event.Connection_Handle);
                break;
            }
            default:
                break;
        }
    }

    void GattClientSt::HandleHciVendorSpecificDebugEvent(const evt_blecore_aci& event)
    {
        switch (event.ecode)
        {
            case ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_att_exchange_mtu_resp_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                    connection->MtuExchanged(response.Server_RX_MTU);
                break;
            }
            case ACI_ATT_READ_BY_GROUP_TYPE_RESP_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_att_read_by_group_type_resp_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                {
                    constexpr uint8_t serviceLengthUuid16 = 6;
                    infra::ByteInputStream stream(infra::ConstByteRange(&response.Attribute_Data_List[0], &response.Attribute_Data_List[0] + response.Data_Length), infra::softFail);
                    connection->ServicesDiscovered(stream, response.Attribute_Data_Length == serviceLengthUuid16);
                }
                break;
            }
            case ACI_ATT_READ_BY_TYPE_RESP_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_att_read_by_type_resp_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                {
                    infra::ByteInputStream stream(infra::ConstByteRange(&response.Handle_Value_Pair_Data[0], &response.Handle_Value_Pair_Data[0] + response.Data_Length), infra::softFail);
                    connection->CharacteristicsOrIncludedServicesDiscovered(stream, response.Handle_Value_Pair_Length);
                }
                break;
            }
            case ACI_ATT_FIND_INFO_RESP_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_att_find_info_resp_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                {
                    infra::ByteInputStream stream(infra::ConstByteRange(&response.Handle_UUID_Pair[0], &response.Handle_UUID_Pair[0] + response.Event_Data_Length), infra::softFail);
                    connection->DescriptorsDiscovered(stream, response.Format == UUID_TYPE_16);
                }
                break;
            }
            case ACI_ATT_READ_RESP_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_att_read_resp_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                    connection->ReadResponse(infra::ConstByteRange(&response.Attribute_Value[0], &response.Attribute_Value[0] + response.Event_Data_Length));
                break;
            }
            case ACI_ATT_READ_BLOB_RESP_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_att_read_blob_resp_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                    connection->ReadBlobResponse(infra::ConstByteRange(&response.Attribute_Value[0], &response.Attribute_Value[0] + response.Event_Data_Length));
                break;
            }
            case ACI_GATT_PROC_COMPLETE_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_gatt_proc_complete_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                    connection->ProcedureComplete(response.Error_Code);
                break;
            }
            case ACI_GATT_NOTIFICATION_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_gatt_notification_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                    connection->NotificationReceived(response.Attribute_Handle, infra::ConstByteRange(&response.Attribute_Value[0], &response.Attribute_Value[0] + response.Attribute_Value_Length));
                break;
            }
            case ACI_GATT_INDICATION_VSEVT_CODE:
            {
                const auto& response = *reinterpret_cast<const aci_gatt_indication_event_rp0*>(event.data);
                if (auto* connection = ConnectionOf(response.Connection_Handle); connection != nullptr)
                    connection->IndicationReceived(response.Attribute_Handle, infra::ConstByteRange(&response.Attribute_Value[0], &response.Attribute_Value[0] + response.Attribute_Value_Length));
                break;
            }
            default:
                break;
        }
    }

    void GattClientSt::HandleConnectionComplete(uint8_t status, uint16_t connectionHandle)
    {
        if (status != BLE_STATUS_SUCCESS || ConnectionOf(connectionHandle) != nullptr)
            return;

        auto connection = connections.Allocate(connectionHandle);

        if (connection == nullptr)
            return;

        established.push_back(connection);

        NotifyObservers([&connection](auto& observer)
            {
                observer.ConnectionEstablished(connection);
            });
    }

    void GattClientSt::HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event)
    {
        auto entry = std::find_if(established.begin(), established.end(), [&event](const auto& each)
            {
                return each->ConnectionHandle() == event.Connection_Handle;
            });

        if (entry == established.end())
            return;

        auto connection = *entry;
        established.erase(entry);

        connection->Released();

        NotifyObservers([&connection](auto& observer)
            {
                observer.ConnectionReleased(*connection);
            });
    }

    GattClientConnectionSt* GattClientSt::ConnectionOf(uint16_t connectionHandle) const
    {
        auto entry = std::find_if(established.begin(), established.end(), [connectionHandle](const auto& each)
            {
                return each->ConnectionHandle() == connectionHandle;
            });

        return entry == established.end() ? nullptr : &**entry;
    }
}
