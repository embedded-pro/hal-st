#include "hal_st/middlewares/ble_middleware/GapCentralSt.hpp"
#include "ble_defs.h"
#include "hal/interfaces/MacAddress.hpp"
#include "hal_st/middlewares/ble_middleware/GapSt.hpp"
#include "infra/event/EventDispatcherWithWeakPtr.hpp"
#include "infra/util/Function.hpp"
#include "services/ble/GapCentral.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace hal
{
    // The parameters this central answers a peripheral's connection parameter update request with,
    // and the ones it opens a connection with when the caller does not name others.
    const services::GapConnectionParameters defaultConnectionUpdateParameters{
        6, // 7.5 ms
        6, // 7.5 ms
        0,
        50, // 500 ms
    };

    const uint16_t minConnectionEventLength = 0;
    const uint16_t maxConnectionEventLength = 0x280; // 400 ms

    const uint8_t filterDuplicatesEnabled = 1;
    const uint8_t rejectParameters = 0;

    // ALL_PHYS with both bits clear states a preference for both directions; PHY_options is
    // meaningful for LE Coded only.
    // Bluetooth Core Specification, Volume 4, Part E, section 7.8.49
    const uint8_t preferBothDirections = 0;
    const uint16_t noPhyOptions = 0;

    namespace
    {
        services::GapAdvertisingEventType ToAdvertisingEventType(uint8_t eventType)
        {
            return static_cast<services::GapAdvertisingEventType>(eventType);
        }

        services::GapDeviceAddressType ToAdvertisingAddressType(uint8_t addressType)
        {
            return static_cast<services::GapDeviceAddressType>(addressType);
        }

        // The command takes a bit field and the event reports an enumerated value, so LE Coded is
        // 0x4 in one and 0x3 in the other.
        // Bluetooth Core Specification, Volume 4, Part E, sections 7.8.49 and 7.7.65.12
        uint8_t ToPhyBit(services::GapPhy phy)
        {
            switch (phy)
            {
                case services::GapPhy::le2M:
                    return 0x2;
                case services::GapPhy::leCoded:
                    return 0x4;
                default:
                    return 0x1;
            }
        }

        services::GapPhy FromPhyValue(uint8_t phy)
        {
            switch (phy)
            {
                case 0x2:
                    return services::GapPhy::le2M;
                case 0x3:
                    return services::GapPhy::leCoded;
                default:
                    return services::GapPhy::le1M;
            }
        }

        services::GapCentral::Result ResultOf(tBleStatus status)
        {
            return status == BLE_STATUS_SUCCESS ? services::GapCentral::Result::success : services::GapCentral::Result::controllerError;
        }

        services::GapRequestStatus RequestStatusOf(tBleStatus status)
        {
            return status == BLE_STATUS_INVALID_PARAMS ? services::GapRequestStatus::invalidParameter : services::GapRequestStatus::invalidState;
        }
    }

    GapCentralSt::GapCentralSt(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration)
        : GapSt(hciEventSource, bondStorageSynchronizer, configuration)
        , connectionParameters(defaultConnectionUpdateParameters)
    {
        Initialize(configuration);

        infra::EventDispatcher::Instance().Schedule([this]
            {
                infra::Subject<services::GapCentralObserver>::NotifyObservers([](auto& observer)
                    {
                        observer.StateChanged(services::GapCentralState::standby);
                    });
            });
    }

    std::optional<services::GapAddress> GapCentralSt::ResolvePrivateAddress(hal::MacAddress address) const
    {
        hal::MacAddress identityAddress;

        if (aci_gap_resolve_private_addr(address.data(), identityAddress.data()) != BLE_STATUS_SUCCESS)
            return std::nullopt;

        // An identity address is either public or static random, and a static random address carries
        // 0b11 in the two most significant bits of its most significant octet.
        // Bluetooth Core Specification, Volume 6, Part B, section 1.3.2.1
        constexpr uint8_t staticRandomAddressMask = 0xc0u;
        auto type = (identityAddress.back() & staticRandomAddressMask) == staticRandomAddressMask ? services::GapDeviceAddressType::randomAddress : services::GapDeviceAddressType::publicAddress;

        return services::GapAddress{ identityAddress, type };
    }

    services::GapRequestStatus GapCentralSt::Connect(const services::GapAddress& peer, const services::GapConnectionParameters& parameters, infra::Duration initiatingTimeout, const infra::Function<void(Result)>& onDone)
    {
        if (connectionContext.connectionHandle != GapSt::invalidConnection)
            return services::GapRequestStatus::invalidState;

        if (!parameters.SupervisionTimeoutIsLongEnough())
            return services::GapRequestStatus::invalidParameter;

        if (onConnectDone)
            return services::GapRequestStatus::busy;

        auto peerAddressType = peer.type == services::GapDeviceAddressType::publicAddress ? GAP_PUBLIC_ADDR : GAP_STATIC_RANDOM_ADDR;

        auto ret = aci_gap_create_connection(
            leScanInterval, leScanWindow, peerAddressType, peer.address.data(), ownAddressType,
            parameters.minConnectionInterval, parameters.maxConnectionInterval,
            parameters.peripheralLatency, parameters.supervisionTimeout,
            minConnectionEventLength, maxConnectionEventLength);

        if (ret != BLE_STATUS_SUCCESS)
            return RequestStatusOf(ret);

        connectionParameters = parameters;
        connectFailureResult = Result::connectionFailed;
        onConnectDone = onDone;

        infra::Subject<services::GapCentralObserver>::NotifyObservers([](auto& observer)
            {
                observer.StateChanged(services::GapCentralState::initiating);
            });

        initiatingStateTimer.Start(initiatingTimeout, [this]()
            {
                connectFailureResult = Result::timeout;
                aci_gap_terminate_gap_proc(GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC);
            });

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapCentralSt::UpdateConnectionParameters(const services::GapConnectionParameters& parameters, const infra::Function<void(Result)>& onDone)
    {
        if (connectionContext.connectionHandle == GapSt::invalidConnection)
            return services::GapRequestStatus::invalidState;

        if (!parameters.SupervisionTimeoutIsLongEnough())
            return services::GapRequestStatus::invalidParameter;

        if (onUpdateConnectionParametersDone)
            return services::GapRequestStatus::busy;

        auto ret = hci_le_connection_update(connectionContext.connectionHandle,
            parameters.minConnectionInterval, parameters.maxConnectionInterval,
            parameters.peripheralLatency, parameters.supervisionTimeout,
            minConnectionEventLength, maxConnectionEventLength);

        if (ret != BLE_STATUS_SUCCESS)
            return RequestStatusOf(ret);

        connectionParameters = parameters;
        onUpdateConnectionParametersDone = onDone;

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapCentralSt::CancelConnect(const infra::Function<void(Result)>& onDone)
    {
        if (!onConnectDone)
            return services::GapRequestStatus::invalidState;

        if (onCancelConnectDone)
            return services::GapRequestStatus::busy;

        initiatingStateTimer.Cancel();

        auto ret = aci_gap_terminate_gap_proc(GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC);

        if (ret != BLE_STATUS_SUCCESS)
            return RequestStatusOf(ret);

        connectFailureResult = Result::cancelled;
        onCancelConnectDone = onDone;

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapCentralSt::Disconnect(const infra::Function<void(Result)>& onDone)
    {
        if (connectionContext.connectionHandle == GapSt::invalidConnection)
            return services::GapRequestStatus::invalidState;

        if (onDisconnectDone)
            return services::GapRequestStatus::busy;

        auto ret = aci_gap_terminate(connectionContext.connectionHandle, remoteUserTerminatedConnection);

        if (ret != BLE_STATUS_SUCCESS)
            return RequestStatusOf(ret);

        onDisconnectDone = onDone;

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapCentralSt::SetAddress(const services::GapAddress& address, const infra::Function<void(Result)>& onDone)
    {
        if (onSetAddressDone)
            return services::GapRequestStatus::busy;

        auto ret = GapSt::SetAddress(address.address, address.type);

        if (ret != BLE_STATUS_SUCCESS)
            return RequestStatusOf(ret);

        onSetAddressDone = onDone;
        Complete(onSetAddressDone, Result::success);

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapCentralSt::SetDataLength(const services::GapDataLength& dataLength)
    {
        if (connectionContext.connectionHandle == GapSt::invalidConnection)
            return services::GapRequestStatus::invalidState;

        auto ret = hci_le_set_data_length(connectionContext.connectionHandle, dataLength.maxTxOctets, dataLength.maxTxTime);

        return ret == BLE_STATUS_SUCCESS ? services::GapRequestStatus::accepted : RequestStatusOf(ret);
    }

    services::GapRequestStatus GapCentralSt::SetPhy(services::GapPhy txPhy, services::GapPhy rxPhy)
    {
        if (connectionContext.connectionHandle == GapSt::invalidConnection)
            return services::GapRequestStatus::invalidState;

        auto ret = hci_le_set_phy(connectionContext.connectionHandle, preferBothDirections, ToPhyBit(txPhy), ToPhyBit(rxPhy), noPhyOptions);

        return ret == BLE_STATUS_SUCCESS ? services::GapRequestStatus::accepted : RequestStatusOf(ret);
    }

    services::GapRequestStatus GapCentralSt::StartDeviceDiscovery(const services::GapScanParameters& parameters, const infra::Function<void(Result)>& onDone)
    {
        // The general discovery procedure sends scan requests, so it always scans actively.
        if (parameters.type != services::GapScanType::active)
            return services::GapRequestStatus::notSupported;

        if (parameters.interval < services::GapScanParameters::intervalMultiplierMin || parameters.interval > services::GapScanParameters::intervalMultiplierMax || parameters.window > parameters.interval)
            return services::GapRequestStatus::invalidParameter;

        if (discovering)
            return services::GapRequestStatus::invalidState;

        if (onStartDeviceDiscoveryDone)
            return services::GapRequestStatus::busy;

        auto ret = aci_gap_start_general_discovery_proc(parameters.interval, parameters.window, ownAddressType, filterDuplicatesEnabled);

        if (ret != BLE_STATUS_SUCCESS)
            return RequestStatusOf(ret);

        discovering = true;

        infra::Subject<services::GapCentralObserver>::NotifyObservers([](auto& observer)
            {
                observer.StateChanged(services::GapCentralState::scanning);
            });

        onStartDeviceDiscoveryDone = onDone;
        Complete(onStartDeviceDiscoveryDone, Result::success);

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapCentralSt::StopDeviceDiscovery(const infra::Function<void(Result)>& onDone)
    {
        if (!discovering)
            return services::GapRequestStatus::invalidState;

        if (onStopDeviceDiscoveryDone)
            return services::GapRequestStatus::busy;

        auto ret = aci_gap_terminate_gap_proc(GAP_GENERAL_DISCOVERY_PROC);

        if (ret != BLE_STATUS_SUCCESS)
            return RequestStatusOf(ret);

        onStopDeviceDiscoveryDone = onDone;

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapCentralSt::AllowPairing(bool allow, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        return services::GapRequestStatus::notSupported;
    }

    void GapCentralSt::HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event)
    {
        GapSt::HandleHciDisconnectEvent(event);

        infra::Subject<services::GapCentralObserver>::NotifyObservers([](auto& observer)
            {
                observer.StateChanged(services::GapCentralState::standby);
            });

        if (onDisconnectDone)
            onDisconnectDone(ResultOf(event.Status));
    }

    void GapCentralSt::HandleHciLeAdvertisingReportEvent(const hci_le_advertising_report_event_rp0& event)
    {
        GapSt::HandleHciLeAdvertisingReportEvent(event);

        for (uint8_t i = 0; i != event.Num_Reports; ++i)
            HandleAdvertisingReport(event.Advertising_Report[i]);
    }

    void GapCentralSt::HandleHciLeConnectionCompleteEvent(const hci_le_connection_complete_event_rp0& event)
    {
        // The connection context is established first, so that a completion callback reaching for
        // the connection it was told about finds it.
        GapSt::HandleHciLeConnectionCompleteEvent(event);
        HandleConnectionCompleteCommon(event.Status);
    }

    void GapCentralSt::HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event)
    {
        GapSt::HandleHciLeEnhancedConnectionCompleteEvent(event);
        HandleConnectionCompleteCommon(event.Status);
    }

    void GapCentralSt::HandleHciLeConnectionUpdateCompleteEvent(const hci_le_connection_update_complete_event_rp0& event)
    {
        GapSt::HandleHciLeConnectionUpdateCompleteEvent(event);

        if (event.Connection_Handle != connectionContext.connectionHandle)
            return;

        if (onUpdateConnectionParametersDone)
            onUpdateConnectionParametersDone(ResultOf(event.Status));
    }

    void GapCentralSt::UpdateStateOnConnectionComplete(uint8_t status)
    {
        services::GapCentralState state = status == BLE_STATUS_SUCCESS ? services::GapCentralState::connected : services::GapCentralState::standby;

        infra::Subject<services::GapCentralObserver>::NotifyObservers([state](services::GapCentralObserver& observer)
            {
                observer.StateChanged(state);
            });
    }

    void GapCentralSt::HandleGapProcedureCompleteEvent(const aci_gap_proc_complete_event_rp0& event)
    {
        GapSt::HandleGapProcedureCompleteEvent(event);

        if (event.Procedure_Code == GAP_LIMITED_DISCOVERY_PROC || event.Procedure_Code == GAP_GENERAL_DISCOVERY_PROC)
            HandleGapDiscoveryProcedureEvent(event.Status);
        else if (event.Procedure_Code == GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC)
            HandleGapDirectConnectionProcedureCompleteEvent();
    }

    void GapCentralSt::HandleGattCompleteEvent(const aci_gatt_proc_complete_event_rp0& event)
    {
        GapSt::HandleGattCompleteEvent(event);

        if (event.Error_Code == BLE_STATUS_SUCCESS)
            really_assert(event.Connection_Handle == connectionContext.connectionHandle);
    }

    void GapCentralSt::HandleL2capConnectionUpdateRequestEvent(const aci_l2cap_connection_update_req_event_rp0& event)
    {
        GapSt::HandleL2capConnectionUpdateRequestEvent(event);

        auto identifier = event.Identifier;

        infra::EventDispatcherWithWeakPtr::Instance().Schedule([this, identifier]()
            {
                auto status = aci_l2cap_connection_parameter_update_resp(
                    connectionContext.connectionHandle, connectionParameters.minConnectionInterval, connectionParameters.maxConnectionInterval,
                    connectionParameters.peripheralLatency, connectionParameters.supervisionTimeout,
                    minConnectionEventLength, maxConnectionEventLength, identifier, rejectParameters);
                assert(status == BLE_STATUS_SUCCESS);
            });
    }

    void GapCentralSt::HandleHciLeDataLengthChangeEvent(const hci_le_data_length_change_event_rp0& event)
    {
        GapSt::HandleHciLeDataLengthChangeEvent(event);

        really_assert(event.Connection_Handle == connectionContext.connectionHandle);

        services::GapDataLength dataLength{ event.MaxTxOctets, event.MaxTxTime };

        infra::Subject<services::GapCentralObserver>::NotifyObservers([&dataLength](auto& observer)
            {
                observer.DataLengthChanged(dataLength);
            });
    }

    void GapCentralSt::HandleHciLePhyUpdateCompleteEvent(const hci_le_phy_update_complete_event_rp0& event)
    {
        GapSt::HandleHciLePhyUpdateCompleteEvent(event);

        really_assert(event.Connection_Handle == connectionContext.connectionHandle);

        // A failed update leaves the link on the PHY it already had, which is no change to report.
        if (event.Status != BLE_STATUS_SUCCESS)
            return;

        auto txPhy = FromPhyValue(event.TX_PHY);
        auto rxPhy = FromPhyValue(event.RX_PHY);

        infra::Subject<services::GapCentralObserver>::NotifyObservers([txPhy, rxPhy](auto& observer)
            {
                observer.PhyUpdated(txPhy, rxPhy);
            });
    }

    void GapCentralSt::HandleGapDiscoveryProcedureEvent(uint8_t status)
    {
        discovering = false;

        infra::Subject<services::GapCentralObserver>::NotifyObservers([](auto& observer)
            {
                observer.StateChanged(services::GapCentralState::standby);
            });

        if (onStopDeviceDiscoveryDone)
            onStopDeviceDiscoveryDone(ResultOf(status));
    }

    void GapCentralSt::HandleGapDirectConnectionProcedureCompleteEvent()
    {
        if (initiatingStateTimer.Armed())
            return;

        // The procedure ended without a connection; a connection reports through the connection
        // complete event instead.
        if (onConnectDone)
            onConnectDone(connectFailureResult);

        if (onCancelConnectDone)
            onCancelConnectDone(Result::success);

        infra::Subject<services::GapCentralObserver>::NotifyObservers([](services::GapCentralObserver& observer)
            {
                observer.StateChanged(services::GapCentralState::standby);
            });
    }

    void GapCentralSt::HandleAdvertisingReport(const Advertising_Report_t& advertisingReport)
    {
        services::GapAdvertisingReport discoveredDevice;

        auto advertisementData = const_cast<uint8_t*>(&advertisingReport.Length_Data) + 1;
        std::copy_n(std::begin(advertisingReport.Address), discoveredDevice.address.size(), std::begin(discoveredDevice.address));
        discoveredDevice.eventType = ToAdvertisingEventType(advertisingReport.Event_Type);
        discoveredDevice.addressType = ToAdvertisingAddressType(advertisingReport.Address_Type);
        discoveredDevice.data = infra::ConstByteRange(advertisementData, advertisementData + advertisingReport.Length_Data);
        discoveredDevice.rssi = static_cast<int8_t>(*const_cast<uint8_t*>(advertisementData + advertisingReport.Length_Data));

        infra::Subject<services::GapCentralObserver>::NotifyObservers([&discoveredDevice](auto& observer)
            {
                observer.DeviceDiscovered(discoveredDevice);
            });
    }

    GapSt::SecureConnection GapCentralSt::SecurityModeAndLevelToSecureConnection(services::GapPairing::SecurityModeAndLevel modeAndLevel) const
    {
        if (modeAndLevel == services::GapPairing::SecurityModeAndLevel::mode1Level1)
            return SecureConnection::notSupported;
        else if (modeAndLevel == services::GapPairing::SecurityModeAndLevel::mode1Level4)
            return SecureConnection::mandatory;

        return SecureConnection::optional;
    }

    void GapCentralSt::Initialize(const Configuration& configuration)
    {
        uint16_t gapServiceHandle, gapDevNameCharHandle, gapAppearanceCharHandle;

        aci_gap_init(GAP_CENTRAL_ROLE, configuration.privacy ? PRIVACY_ENABLED : PRIVACY_DISABLED, configuration.gapService.deviceName.size(), &gapServiceHandle, &gapDevNameCharHandle, &gapAppearanceCharHandle);
        aci_gatt_update_char_value(gapServiceHandle, gapDevNameCharHandle, 0, configuration.gapService.deviceName.size(), reinterpret_cast<const uint8_t*>(configuration.gapService.deviceName.data()));
        aci_gatt_update_char_value(gapServiceHandle, gapAppearanceCharHandle, 0, sizeof(configuration.gapService.appearance), reinterpret_cast<const uint8_t*>(&configuration.gapService.appearance));

        SetIoCapabilities(configuration.security.ioCapabilities, [](services::GapPairingResult) {});
        SetSecurityMode(configuration.security.modeAndLevel, [](services::GapPairingResult) {});
    }

    void GapCentralSt::HandleConnectionCompleteCommon(uint8_t status)
    {
        UpdateStateOnConnectionComplete(status);
        initiatingStateTimer.Cancel();

        if (onConnectDone)
            onConnectDone(status == BLE_STATUS_SUCCESS ? Result::success : Result::connectionFailed);
    }
}
