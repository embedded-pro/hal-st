#include "hal_st/middlewares/ble_middleware/GapPeripheralSt.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "services/ble/GapPeripheral.hpp"

namespace
{
    uint8_t ConvertAdvertisementType(services::GapAdvertisementType type)
    {
        switch (type)
        {
            case services::GapAdvertisementType::advScanInd:
                return ADV_SCAN_IND;
            case services::GapAdvertisementType::advNonconnInd:
                return ADV_NONCONN_IND;
            default:
                return ADV_IND;
        }
    }

    // The Advertising_Type values of the two directed forms. ST names the low duty cycle one only
    // on WBA, so both are taken from the specification.
    // Bluetooth Core Specification, Volume 4, Part E, section 7.8.5
    constexpr uint8_t highDutyCycleDirectedAdvertising = 0x01u;
    constexpr uint8_t lowDutyCycleDirectedAdvertising = 0x04u;

    uint8_t ConvertDirectedAdvertisementType(services::GapDirectedAdvertisementType type)
    {
        return type == services::GapDirectedAdvertisementType::highDutyCycle ? highDutyCycleDirectedAdvertising : lowDutyCycleDirectedAdvertising;
    }

    // Bluetooth Core Specification, Volume 1, Part F, section 1.3
    constexpr uint8_t remoteUserTerminatedConnection = 0x13u;

    // Connection Parameters accepted, ACI_L2CAP_CONNECTION_UPDATE_RESP_EVENT
    constexpr uint16_t connectionParametersAccepted = 0x0000u;

    services::GapPeripheral::Result ResultOf(tBleStatus status)
    {
        if (status == BLE_STATUS_SUCCESS)
            return services::GapPeripheral::Result::success;
        if (status == BLE_STATUS_INVALID_PARAMS)
            return services::GapPeripheral::Result::invalidParameter;

        return services::GapPeripheral::Result::controllerError;
    }
}

namespace hal
{
    GapPeripheralSt::GapPeripheralSt(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration)
        : GapSt(hciEventSource, bondStorageSynchronizer, configuration)
    {
        Initialize(configuration);
    }

    services::GapAddress GapPeripheralSt::GetAddress() const
    {
        services::GapAddress address;
        /* Use last peer addres to get current RPA */
        [[maybe_unused]] auto status = hci_le_read_local_resolvable_address(static_cast<uint8_t>(connectionContext.peerAddressType), connectionContext.peerAddress.data(), address.address.data());

        address.type = services::GapDeviceAddressType::randomAddress;

        assert(status == BLE_STATUS_SUCCESS);

        return address;
    }

    services::GapAddress GapPeripheralSt::GetIdentityAddress() const
    {
        services::GapAddress address;
        uint8_t length = 0;

        aci_hal_read_config_data(CONFIG_DATA_PUBADDR_OFFSET, &length, address.address.data());
        address.type = services::GapDeviceAddressType::publicAddress;

        return address;
    }

    infra::ConstByteRange GapPeripheralSt::GetAdvertisementData() const
    {
        return infra::MakeRange(advertisementData);
    }

    infra::ConstByteRange GapPeripheralSt::GetScanResponseData() const
    {
        return infra::MakeRange(scanResponseData);
    }

    services::GapRequestStatus GapPeripheralSt::SetAdvertisementData(infra::ConstByteRange data, const infra::Function<void(Result)>& onDone)
    {
        if (data.size() > advertisementData.max_size())
            return services::GapRequestStatus::invalidParameter;

        if (onSetAdvertisementDataDone)
            return services::GapRequestStatus::busy;

        advertisementData.assign(data);

        onSetAdvertisementDataDone = onDone;
        Complete(onSetAdvertisementDataDone, Result::success);

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapPeripheralSt::SetScanResponseData(infra::ConstByteRange data, const infra::Function<void(Result)>& onDone)
    {
        if (data.size() > scanResponseData.max_size())
            return services::GapRequestStatus::invalidParameter;

        if (onSetScanResponseDataDone)
            return services::GapRequestStatus::busy;

        scanResponseData.assign(data);

        onSetScanResponseDataDone = onDone;
        Complete(onSetScanResponseDataDone, Result::success);

        return services::GapRequestStatus::accepted;
    }

    void GapPeripheralSt::UpdateAdvertisementData()
    {
        // First clear the data set by the aci_gap_set_discoverable call by default
        aci_gap_delete_ad_type(AD_TYPE_TX_POWER_LEVEL);
        aci_gap_delete_ad_type(AD_TYPE_FLAGS);

        aci_gap_update_adv_data(advertisementData.size(), advertisementData.begin());

        hci_le_set_scan_response_data(scanResponseData.size(), scanResponseData.begin());
    }

    void GapPeripheralSt::UpdateState(services::GapPeripheralState newState)
    {
        state = newState;
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                GapPeripheral::NotifyObservers([this](auto& obs)
                    {
                        obs.StateChanged(state);
                    });
            });
    }

    services::GapRequestStatus GapPeripheralSt::Advertise(const services::GapAdvertisingParameters& parameters, const infra::Function<void(Result)>& onDone)
    {
        if (parameters.interval < advertisementIntervalMultiplierMin || parameters.interval > advertisementIntervalMultiplierMax)
            return services::GapRequestStatus::invalidParameter;

        // The GAP advertising procedures of the ST stack take no advertising channel map. Only
        // HCI_LE_SET_ADVERTISING_PARAMETERS does, and that bypasses the GAP state machine which
        // owns advertising here.
        if (parameters.channels != services::GapAdvertisingChannels::all)
            return services::GapRequestStatus::notSupported;

        if (onAdvertiseDone)
            return services::GapRequestStatus::busy;

        UpdateResolvingList();

        tBleStatus ret = BLE_STATUS_INVALID_PARAMS;

        if (allowPairing)
        {
            StartedAdvertising("aci_gap_set_discoverable");
            ret = aci_gap_set_discoverable(ConvertAdvertisementType(parameters.type), parameters.interval, parameters.interval, ownAddressType, static_cast<uint8_t>(parameters.filterPolicy), 0, NULL, 0, NULL, 0, 0);
        }
        else
        {
            // Advertising with pairing disallowed exists to reach bonded devices only, so it keeps
            // filtering on the whitelist whatever filter policy was asked for.
            StartedAdvertising("aci_gap_set_undirected_connectable");
            ret = aci_gap_set_undirected_connectable(parameters.interval, parameters.interval, ownAddressType, WHITE_LIST_FOR_ALL);
        }

        UpdateAdvertisementData();

        if (ret == BLE_STATUS_SUCCESS)
            UpdateState(services::GapPeripheralState::advertising);

        onAdvertiseDone = onDone;
        Complete(onAdvertiseDone, ResultOf(ret));

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapPeripheralSt::AdvertiseDirected(services::GapDirectedAdvertisementType type, const services::GapAddress& peer, AdvertisementIntervalMultiplier multiplier, const infra::Function<void(Result)>& onDone)
    {
        if (type == services::GapDirectedAdvertisementType::lowDutyCycle && (multiplier < advertisementIntervalMultiplierMin || multiplier > advertisementIntervalMultiplierMax))
            return services::GapRequestStatus::invalidParameter;

        if (onAdvertiseDirectedDone)
            return services::GapRequestStatus::busy;

        UpdateResolvingList();

        auto peerAddressType = peer.type == services::GapDeviceAddressType::publicAddress ? GAP_PUBLIC_ADDR : GAP_STATIC_RANDOM_ADDR;

        StartedAdvertising("aci_gap_set_direct_connectable");
        auto ret = aci_gap_set_direct_connectable(ownAddressType, ConvertDirectedAdvertisementType(type), peerAddressType, peer.address.data(), multiplier, multiplier);

        if (ret == BLE_STATUS_SUCCESS)
            UpdateState(services::GapPeripheralState::advertising);

        onAdvertiseDirectedDone = onDone;
        Complete(onAdvertiseDirectedDone, ResultOf(ret));

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapPeripheralSt::Standby(const infra::Function<void(Result)>& onDone)
    {
        if (onStandbyDone)
            return services::GapRequestStatus::busy;

        if (connectionContext.connectionHandle != GapSt::invalidConnection)
        {
            auto ret = aci_gap_terminate(connectionContext.connectionHandle, remoteUserTerminatedConnection);

            if (ret != BLE_STATUS_SUCCESS)
                return services::GapRequestStatus::invalidState;

            // Standby is reached when the link is gone, so the disconnect event completes this.
            onStandbyDone = onDone;

            return services::GapRequestStatus::accepted;
        }

        auto ret = aci_gap_set_non_discoverable();

        if (ret == BLE_STATUS_SUCCESS)
            UpdateState(services::GapPeripheralState::standby);

        onStandbyDone = onDone;
        Complete(onStandbyDone, ResultOf(ret));

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapPeripheralSt::RequestConnectionParameterUpdate(const services::GapConnectionParameters& connParam, const infra::Function<void(Result)>& onDone)
    {
        if (connectionContext.connectionHandle == GapSt::invalidConnection)
            return services::GapRequestStatus::invalidState;

        if (!connParam.SupervisionTimeoutIsLongEnough())
            return services::GapRequestStatus::invalidParameter;

        if (onRequestConnectionParameterUpdateDone)
            return services::GapRequestStatus::busy;

        auto ret = aci_l2cap_connection_parameter_update_req(connectionContext.connectionHandle,
            connParam.minConnectionInterval, connParam.maxConnectionInterval,
            connParam.peripheralLatency, connParam.supervisionTimeout);

        if (ret != BLE_STATUS_SUCCESS)
            return ret == BLE_STATUS_INVALID_PARAMS ? services::GapRequestStatus::invalidParameter : services::GapRequestStatus::invalidState;

        onRequestConnectionParameterUpdateDone = onDone;

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapPeripheralSt::AllowPairing(bool allow, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        if (onAllowPairingDone)
            return services::GapRequestStatus::busy;

        allowPairing = allow;

        onAllowPairingDone = onDone;
        Complete(onAllowPairingDone, services::GapPairingResult::success);

        return services::GapRequestStatus::accepted;
    }

    void GapPeripheralSt::UpdateResolvingList()
    {
        aci_gap_configure_whitelist();

        uint8_t numberOfBondedAddress;
        std::array<Bonded_Device_Entry_t, maxNumberOfBonds> bondedDevices;
        aci_gap_get_bonded_devices(&numberOfBondedAddress, bondedDevices.data());

        ReceivedNumberOfBondedAddresses(numberOfBondedAddress);

        if (numberOfBondedAddress == 0)
        {
            aci_gap_add_devices_to_resolving_list(1, &dummyPeer, 1);

            std::copy(std::begin(dummyPeer.Peer_Identity_Address), std::end(dummyPeer.Peer_Identity_Address), connectionContext.peerAddress.begin());
            connectionContext.peerAddressType = static_cast<services::GapDeviceAddressType>(dummyPeer.Peer_Identity_Address_Type);
        }
        else
        {
            aci_gap_add_devices_to_resolving_list(numberOfBondedAddress, reinterpret_cast<const Whitelist_Identity_Entry_t*>(bondedDevices.begin()), 1);

            std::copy(std::begin(bondedDevices[numberOfBondedAddress - 1].Address), std::end(bondedDevices[numberOfBondedAddress - 1].Address), connectionContext.peerAddress.begin());
            connectionContext.peerAddressType = static_cast<services::GapDeviceAddressType>(bondedDevices[numberOfBondedAddress - 1].Address_Type);

            for (uint8_t i = 0; i < numberOfBondedAddress; i++)
                hci_le_set_privacy_mode(bondedDevices[i].Address_Type, bondedDevices[i].Address, HCI_PRIV_MODE_DEVICE);
        }
    }

    void GapPeripheralSt::ClearResolvingList()
    {
        aci_gap_configure_whitelist();
        aci_gap_add_devices_to_resolving_list(0, nullptr, 1);
    }

    void GapPeripheralSt::HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event)
    {
        GapSt::HandleHciDisconnectEvent(event);
        UpdateState(services::GapPeripheralState::standby);

        if (onStandbyDone)
            onStandbyDone(ResultOf(event.Status));
    }

    void GapPeripheralSt::HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event)
    {
        GapSt::HandleHciLeEnhancedConnectionCompleteEvent(event);

        UpdateState(services::GapPeripheralState::connected);
    }

    void GapPeripheralSt::HandleL2capConnectionUpdateResponseEvent(const aci_l2cap_connection_update_resp_event_rp0& event)
    {
        if (event.Connection_Handle != connectionContext.connectionHandle)
            return;

        if (onRequestConnectionParameterUpdateDone)
            onRequestConnectionParameterUpdateDone(event.Result == connectionParametersAccepted ? Result::success : Result::invalidParameter);
    }

    void GapPeripheralSt::Initialize(const Configuration& configuration)
    {
        uint16_t gapServiceHandle, gapDevNameCharHandle, gapAppearanceCharHandle;

        aci_gap_init(GAP_PERIPHERAL_ROLE, configuration.privacy ? PRIVACY_ENABLED : PRIVACY_DISABLED, configuration.gapService.deviceName.size(), &gapServiceHandle, &gapDevNameCharHandle, &gapAppearanceCharHandle);
        aci_gatt_update_char_value(gapServiceHandle, gapDevNameCharHandle, 0, configuration.gapService.deviceName.size(), reinterpret_cast<const uint8_t*>(configuration.gapService.deviceName.data()));
        aci_gatt_update_char_value(gapServiceHandle, gapAppearanceCharHandle, 0, sizeof(configuration.gapService.appearance), reinterpret_cast<const uint8_t*>(&configuration.gapService.appearance));

        SetIoCapabilities(configuration.security.ioCapabilities, [](services::GapPairingResult) {});
        SetSecurityMode(configuration.security.modeAndLevel, [](services::GapPairingResult) {});

        // The suggested transmission time is the one a connection on LE 1M needs, which is the
        // longest of the two PHYs this port uses.
        hci_le_write_suggested_default_data_length(services::GapDataLength::initialMaxTxOctets, services::GapDataLength::InitialMaxTxTime(services::GapPhy::le1M));
        hci_le_set_default_phy(allPhys, speed2Mbps, speed2Mbps);
    }
}
