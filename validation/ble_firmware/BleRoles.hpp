#pragma once

#include "hal_st/middlewares/ble_middleware/GapCentralSt.hpp"
#include "hal_st/middlewares/ble_middleware/GapPeripheralSt.hpp"
#include "hal_st/middlewares/ble_middleware/GattClientSt.hpp"
#include "hal_st/middlewares/ble_middleware/GattServerSt.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/SharedPtr.hpp"
#include "services/ble/GapAdvertisingData.hpp"
#include "services/ble/GattClient.hpp"
#include "services/ble/profile/GenericAttributeService.hpp"
#include "services/ble/profile/NordicUartCentral.hpp"
#include "services/ble/profile/NordicUartPeripheral.hpp"
#include "validation/ble_firmware/BleReporter.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    inline constexpr uint16_t bleMaxAttMtuSize = 251;
    inline constexpr std::size_t bleMaxBonds = 10;
    inline constexpr std::size_t bleNameCapacity = 16;

    template<class Gap>
    class ReportingGap
        : public Gap
    {
    public:
        ReportingGap(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer,
            const hal::GapSt::Configuration& configuration, BleReporter& reporter)
            : Gap(hciEventSource, bondStorageSynchronizer, configuration)
            , reporter(reporter)
        {}

    protected:
        void HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event) override
        {
            Gap::HandleHciDisconnectEvent(event);

            if (event.Status == BLE_STATUS_SUCCESS)
                reporter.Disconnected(event.Reason);
        }

        void HandleHciLeConnectionCompleteEvent(const hci_le_connection_complete_event_rp0& event) override
        {
            Gap::HandleHciLeConnectionCompleteEvent(event);

            if (event.Status == BLE_STATUS_SUCCESS)
                ReportConnected(event.Peer_Address, event.Conn_Interval, event.Conn_Latency, event.Supervision_Timeout);
        }

        void HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event) override
        {
            Gap::HandleHciLeEnhancedConnectionCompleteEvent(event);

            if (event.Status == BLE_STATUS_SUCCESS)
                ReportConnected(event.Peer_Address, event.Conn_Interval, event.Conn_Latency, event.Supervision_Timeout);
        }

        void HandleHciLeConnectionUpdateCompleteEvent(const hci_le_connection_update_complete_event_rp0& event) override
        {
            Gap::HandleHciLeConnectionUpdateCompleteEvent(event);

            if (event.Status == BLE_STATUS_SUCCESS)
                reporter.ConnectionParameters(event.Conn_Interval, event.Conn_Latency, event.Supervision_Timeout);
        }

        void HandleHciLeDataLengthChangeEvent(const hci_le_data_length_change_event_rp0& event) override
        {
            Gap::HandleHciLeDataLengthChangeEvent(event);
            reporter.DataLength(event.MaxTxOctets, event.MaxTxTime, event.MaxRxOctets, event.MaxRxTime);
        }

        void HandleHciLePhyUpdateCompleteEvent(const hci_le_phy_update_complete_event_rp0& event) override
        {
            Gap::HandleHciLePhyUpdateCompleteEvent(event);

            if (event.Status == BLE_STATUS_SUCCESS)
                reporter.Phy(event.TX_PHY, event.RX_PHY);
        }

    private:
        void ReportConnected(const uint8_t (&peerAddress)[6], uint16_t interval, uint16_t latency, uint16_t timeout)
        {
            hal::MacAddress peer{};
            std::copy(std::begin(peerAddress), std::end(peerAddress), peer.begin());
            reporter.Connected(peer, interval, latency, timeout);
        }

    private:
        BleReporter& reporter;
    };

    class NordicUartGattServer
        : public hal::GattServerSt
    {
    public:
        NordicUartGattServer(hal::HciEventSource& hciEventSource, BleReporter& reporter);

        void AttachNordicUart(services::NordicUartPeripheral& nordicUart);

        void HciEvent(hci_event_pckt& event) override;

    protected:
        void HandleGattAttributeModified(aci_gatt_attribute_modified_event_rp0& event) override;

    private:
        BleReporter& reporter;
        services::NordicUartPeripheral* nordicUart = nullptr;
    };

    class PairingReporter
        : private services::GapPairingObserver
    {
    public:
        PairingReporter(services::GapPairing& pairing, BleReporter& reporter);

    private:
        void DisplayPasskey(uint32_t passkey) override;
        void ConfirmNumericComparison(uint32_t value) override;
        void PairingSuccessfullyCompleted(const services::GapBondStrength& strength) override;
        void PairingFailed(services::GapPairingResult error) override;
        void OutOfBandDataGenerated(const services::GapOutOfBandData& outOfBandData) override;

    private:
        BleReporter& reporter;
    };

    class UartReporter
        : public services::NordicUartObserver
    {
    public:
        explicit UartReporter(BleReporter& reporter);

        void Opened() override;
        void Closed() override;

    private:
        BleReporter& reporter;
    };

    class PeripheralRole
        : private services::GapPeripheralObserver
    {
    public:
        PeripheralRole(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer,
            const hal::MacAddress& address, BleReporter& reporter);

        const hal::MacAddress& Address() const;
        hal::GapPeripheralSt& Gap();
        services::NordicUart& Uart();
        services::GapPeripheralState State() const;

        services::GapRequestStatus Advertise(uint32_t intervalMs, infra::BoundedConstString name);

    private:
        services::GapRequestStatus SetAdvertisementData();
        void StartAdvertising(services::GapPeripheral::Result result);

        void StateChanged(services::GapPeripheralState newState) override;

    private:
        BleReporter& reporter;
        hal::MacAddress address;
        hal::GapSt::GapService gapService{ "hal-st-periph", 0 };
        hal::GapSt::Configuration gapConfiguration;
        ReportingGap<hal::GapPeripheralSt> gap;
        NordicUartGattServer gattServer;
        hal::GattConfirmIndication confirmIndication;
        services::GenericAttributeService genericAttributeService;
        services::NordicUartPeripheral nordicUart;
        PairingReporter pairingReporter;
        UartReporter uartReporter;

        services::GapPeripheralState state = services::GapPeripheralState::standby;
        infra::BoundedVector<uint8_t>::WithMaxSize<services::gapMaxAdvertisementDataSize> advertisementData;
        services::GapPeripheral::AdvertisementIntervalMultiplier advertisementInterval = 0;
    };

    class CentralRole
        : private services::GapCentralObserver
        , private services::GattClientObserver
    {
    public:
        CentralRole(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer,
            const hal::MacAddress& address, BleReporter& reporter);

        const hal::MacAddress& Address() const;
        hal::GapCentralSt& Gap();
        services::GapCentralState State() const;

        services::NordicUart* Uart();

        services::GapRequestStatus StartScan(const std::optional<hal::MacAddress>& addressFilter, const std::optional<infra::BoundedConstString>& nameFilter, std::optional<uint32_t> timeoutMs);
        services::GapRequestStatus StopScan();
        services::GattRequestStatus ExchangeMtu();
        services::GattRequestStatus Discover();

    private:
        bool Wanted(const services::GapAdvertisingReport& report) const;

        void DeviceDiscovered(const services::GapAdvertisingReport& deviceDiscovered) override;
        void StateChanged(services::GapCentralState newState) override;
        void PhyUpdated(services::GapPhy txPhy, services::GapPhy rxPhy) override;
        void DataLengthChanged(const services::GapDataLength& dataLength) override;

        void ConnectionEstablished(infra::SharedPtr<services::GattClientConnection> establishedConnection) override;
        void ConnectionReleased(services::GattClientConnection& releasedConnection) override;

    private:
        static constexpr std::size_t scanTableSize = 8;

        BleReporter& reporter;
        hal::MacAddress address;
        hal::GapSt::GapService gapService{ "hal-st-central", 0 };
        hal::GapSt::Configuration gapConfiguration;
        ReportingGap<hal::GapCentralSt> gap;
        hal::GattClientSt::WithMaxConnections<1> gattClient;
        PairingReporter pairingReporter;
        UartReporter uartReporter;

        services::GapCentralState state = services::GapCentralState::standby;
        infra::SharedPtr<services::GattClientConnection> connection;
        std::optional<services::NordicUartCentral> nordicUart;

        std::optional<hal::MacAddress> scanAddress;
        infra::BoundedString::WithStorage<bleNameCapacity> scanName;
        std::array<hal::MacAddress, scanTableSize> scanned{};
        std::size_t scannedCount = 0;
        infra::TimerSingleShot scanTimer;
    };
}
