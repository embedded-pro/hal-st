#include "validation/ble_firmware/BleRoles.hpp"
#include "infra/util/EnumCast.hpp"
#include "services/ble/profile/NordicUart.hpp"
#include <chrono>

namespace validation
{
    namespace
    {
        constexpr uint8_t txPowerLevel = 0x18;

        const hal::GapSt::RootKeys rootKeys{
            { { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } },
            { { 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 } }
        };

        const char* StateName(services::GapPeripheralState state)
        {
            switch (state)
            {
                case services::GapPeripheralState::advertising:
                    return "advertising";
                case services::GapPeripheralState::connected:
                    return "connected";
                default:
                    return "standby";
            }
        }

        const char* StateName(services::GapCentralState state)
        {
            switch (state)
            {
                case services::GapCentralState::scanning:
                    return "scanning";
                case services::GapCentralState::initiating:
                    return "connecting";
                case services::GapCentralState::connected:
                    return "connected";
                default:
                    return "standby";
            }
        }
    }

    NordicUartGattServer::NordicUartGattServer(hal::HciEventSource& hciEventSource, BleReporter& reporter)
        : hal::GattServerSt(hciEventSource)
        , reporter(reporter)
    {}

    void NordicUartGattServer::AttachNordicUart(services::NordicUartPeripheral& nordicUart)
    {
        this->nordicUart = &nordicUart;
    }

    void NordicUartGattServer::HciEvent(hci_event_pckt& event)
    {
        hal::GattServerSt::HciEvent(event);

        if (nordicUart == nullptr)
            return;

        if (event.evt == HCI_DISCONNECTION_COMPLETE_EVT_CODE)
            return nordicUart->NotificationsEnabled(false);

        if (event.evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE)
            return;

        const auto& coreEvent = *reinterpret_cast<evt_blecore_aci*>(event.data);

        if (coreEvent.ecode == ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE)
        {
            const auto& response = *reinterpret_cast<const aci_att_exchange_mtu_resp_event_rp0*>(coreEvent.data);
            nordicUart->MaxAttMtuSizeChanged(response.Server_RX_MTU);
            reporter.Mtu(response.Server_RX_MTU);
        }
    }

    void NordicUartGattServer::HandleGattAttributeModified(aci_gatt_attribute_modified_event_rp0& event)
    {
        hal::GattServerSt::HandleGattAttributeModified(event);

        if (nordicUart == nullptr || event.Attr_Data_Length < sizeof(uint16_t))
            return;

        constexpr uint16_t clientCharacteristicConfigurationOffset = 2;

        for (const auto& characteristic : nordicUart->Service().Characteristics())
            if (characteristic.Type() == services::AttAttribute::Uuid{ services::uuid::nordicUartTx } &&
                event.Attr_Handle == characteristic.Handle() + clientCharacteristicConfigurationOffset)
            {
                const auto value = static_cast<uint16_t>(event.Attr_Data[0] | (event.Attr_Data[1] << 8));
                nordicUart->NotificationsEnabled(value == infra::enum_cast(services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue::enableNotification));
            }
    }

    PairingReporter::PairingReporter(services::GapPairing& pairing, BleReporter& reporter)
        : services::GapPairingObserver(pairing)
        , reporter(reporter)
    {}

    void PairingReporter::DisplayPasskey(uint32_t passkey)
    {}

    void PairingReporter::ConfirmNumericComparison(uint32_t value)
    {}

    void PairingReporter::PairingSuccessfullyCompleted(const services::GapBondStrength& strength)
    {
        reporter.Paired(strength);
    }

    void PairingReporter::PairingFailed(services::GapPairingResult error)
    {
        reporter.PairingFailed(error);
    }

    void PairingReporter::OutOfBandDataGenerated(const services::GapOutOfBandData& outOfBandData)
    {}

    UartReporter::UartReporter(BleReporter& reporter)
        : reporter(reporter)
    {}

    void UartReporter::Opened()
    {
        reporter.Uart(true, Subject().MaxSendSize());
    }

    void UartReporter::Closed()
    {
        reporter.Uart(false, 0);
    }

    PeripheralRole::PeripheralRole(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer,
        const hal::MacAddress& address, BleReporter& reporter)
        : reporter(reporter)
        , address(address)
        , gapConfiguration{ this->address, gapService, rootKeys, hal::GapSt::justWorks, txPowerLevel, false }
        , gap(hciEventSource, bondStorageSynchronizer, gapConfiguration, reporter)
        , gattServer(hciEventSource, reporter)
        , confirmIndication(hciEventSource)
        , genericAttributeService(gattServer)
        , nordicUart(gattServer, bleMaxAttMtuSize)
        , pairingReporter(gap, reporter)
        , uartReporter(reporter)
    {
        gattServer.AttachNordicUart(nordicUart);
        services::GapPeripheralObserver::Attach(gap);
        uartReporter.Attach(nordicUart);
        nordicUart.ReceiveData([this](infra::ConstByteRange data)
            {
                this->reporter.Received(data);
            });
    }

    const hal::MacAddress& PeripheralRole::Address() const
    {
        return address;
    }

    hal::GapPeripheralSt& PeripheralRole::Gap()
    {
        return gap;
    }

    services::NordicUart& PeripheralRole::Uart()
    {
        return nordicUart;
    }

    services::GapPeripheralState PeripheralRole::State() const
    {
        return state;
    }

    services::GapRequestStatus PeripheralRole::Advertise(uint32_t intervalMs, infra::BoundedConstString name)
    {
        if (state == services::GapPeripheralState::connected)
            return services::GapRequestStatus::invalidState;

        advertisementInterval = static_cast<services::GapPeripheral::AdvertisementIntervalMultiplier>(intervalMs * 8 / 5);

        advertisementData.clear();
        services::GapAdvertisementFormatter formatter{ advertisementData };
        formatter.AppendFlags(services::GapAdvertisementFlags::leGeneralDiscoverableMode | services::GapAdvertisementFlags::brEdrNotSupported);
        formatter.AppendCompleteLocalName(name);

        if (state == services::GapPeripheralState::advertising)
            return gap.Standby([this](services::GapPeripheral::Result)
                {
                    auto status = SetAdvertisementData();

                    if (status != services::GapRequestStatus::accepted)
                        reporter.AdvertiseFailed(static_cast<uint32_t>(status));
                });

        return SetAdvertisementData();
    }

    services::GapRequestStatus PeripheralRole::SetAdvertisementData()
    {
        return gap.SetAdvertisementData(infra::MakeRange(advertisementData), [this](services::GapPeripheral::Result result)
            {
                StartAdvertising(result);
            });
    }

    void PeripheralRole::StartAdvertising(services::GapPeripheral::Result result)
    {
        if (result != services::GapPeripheral::Result::success)
            return reporter.AdvertiseFailed(static_cast<uint32_t>(result));

        auto status = gap.Advertise(services::GapAdvertisementType::advInd, advertisementInterval, [this](services::GapPeripheral::Result advertiseResult)
            {
                if (advertiseResult != services::GapPeripheral::Result::success)
                    reporter.AdvertiseFailed(static_cast<uint32_t>(advertiseResult));
            });

        if (status != services::GapRequestStatus::accepted)
            reporter.AdvertiseFailed(100 + static_cast<uint32_t>(status));
    }

    void PeripheralRole::StateChanged(services::GapPeripheralState newState)
    {
        state = newState;
        reporter.State(StateName(newState));
    }

    CentralRole::CentralRole(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer,
        const hal::MacAddress& address, BleReporter& reporter)
        : reporter(reporter)
        , address(address)
        , gapConfiguration{ this->address, gapService, rootKeys, hal::GapSt::justWorks, txPowerLevel, false }
        , gap(hciEventSource, bondStorageSynchronizer, gapConfiguration, reporter)
        , gattClient(hciEventSource)
        , pairingReporter(gap, reporter)
        , uartReporter(reporter)
    {
        services::GapCentralObserver::Attach(gap);
        services::GattClientObserver::Attach(gattClient);
    }

    const hal::MacAddress& CentralRole::Address() const
    {
        return address;
    }

    hal::GapCentralSt& CentralRole::Gap()
    {
        return gap;
    }

    services::GapCentralState CentralRole::State() const
    {
        return state;
    }

    services::NordicUart* CentralRole::Uart()
    {
        return nordicUart ? &*nordicUart : nullptr;
    }

    services::GapRequestStatus CentralRole::StartScan(const std::optional<hal::MacAddress>& addressFilter, const std::optional<infra::BoundedConstString>& nameFilter, std::optional<uint32_t> timeoutMs)
    {
        scanAddress = addressFilter;
        scanName.clear();

        if (nameFilter)
            scanName.assign(nameFilter->substr(0, std::min(nameFilter->size(), scanName.max_size())));

        scannedCount = 0;

        auto status = gap.StartDeviceDiscovery([](services::GapCentral::Result) {});

        if (status == services::GapRequestStatus::accepted && timeoutMs)
            scanTimer.Start(std::chrono::milliseconds(*timeoutMs), [this]()
                {
                    StopScan();
                });

        return status;
    }

    services::GapRequestStatus CentralRole::StopScan()
    {
        scanTimer.Cancel();

        return gap.StopDeviceDiscovery([](services::GapCentral::Result) {});
    }

    services::GattRequestStatus CentralRole::ExchangeMtu()
    {
        if (!connection)
            return services::GattRequestStatus::invalidState;

        return connection->ExchangeMtu([this](services::GattResult result)
            {
                if (result == services::GattResult::success && connection)
                    reporter.Mtu(connection->EffectiveMaxAttMtuSize());
            });
    }

    services::GattRequestStatus CentralRole::Discover()
    {
        if (!nordicUart)
            return services::GattRequestStatus::invalidState;

        return nordicUart->Discover([this](services::GattResult result)
            {
                if (result != services::GattResult::success)
                    reporter.UartFailed(static_cast<uint32_t>(result));
            });
    }

    bool CentralRole::Wanted(const services::GapAdvertisingReport& report) const
    {
        const auto scannedEnd = scanned.begin() + scannedCount;

        if (scannedCount == scanned.size() || std::find(scanned.begin(), scannedEnd, report.address) != scannedEnd)
            return false;

        if (scanAddress && report.address != *scanAddress)
            return false;

        if (scanName.empty())
            return true;

        const auto name = services::GapAdvertisingDataParser(report.data).LocalName();
        return name.size() == scanName.size() && std::equal(name.begin(), name.end(), scanName.begin());
    }

    void CentralRole::DeviceDiscovered(const services::GapAdvertisingReport& deviceDiscovered)
    {
        if (!Wanted(deviceDiscovered))
            return;

        scanned[scannedCount++] = deviceDiscovered.address;
        reporter.Found(deviceDiscovered);
    }

    void CentralRole::StateChanged(services::GapCentralState newState)
    {
        state = newState;
        reporter.State(StateName(newState));
    }

    void CentralRole::PhyUpdated(services::GapPhy txPhy, services::GapPhy rxPhy)
    {}

    void CentralRole::DataLengthChanged(const services::GapDataLength& dataLength)
    {}

    void CentralRole::ConnectionEstablished(infra::SharedPtr<services::GattClientConnection> establishedConnection)
    {
        connection = establishedConnection;
        nordicUart.emplace(*connection);
        uartReporter.Attach(*nordicUart);
        nordicUart->ReceiveData([this](infra::ConstByteRange data)
            {
                reporter.Received(data);
            });
    }

    void CentralRole::ConnectionReleased(services::GattClientConnection& releasedConnection)
    {
        if (nordicUart)
        {
            nordicUart->Close();

            if (uartReporter.Attached())
                uartReporter.Detach();

            nordicUart = std::nullopt;
        }

        connection = nullptr;
    }
}
