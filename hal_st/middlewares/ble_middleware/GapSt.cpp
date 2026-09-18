#include "hal_st/middlewares/ble_middleware/GapSt.hpp"
#include "ble_defs.h"
#include "ble_gap_aci.h"
#include "ble_types.h"
#include "infra/event/EventDispatcherWithWeakPtr.hpp"
#include "services/ble/GapPairing.hpp"
#include <utility>

namespace hal
{
    namespace
    {
        // The reason codes ST reports in ACI_GAP_PAIRING_COMPLETE_EVENT are the Pairing Failed
        // reason codes of the Security Manager. ST names only a part of them, and names those
        // differently on WB and WBA, so the values are taken from the specification instead.
        // Bluetooth Core Specification, Volume 3, Part H, section 3.5.5, Table 3.7
        enum class PairingFailedReason : uint8_t
        {
            passkeyEntryFailed = 0x01u,
            oobNotAvailable = 0x02u,
            authenticationRequirements = 0x03u,
            confirmValueFailed = 0x04u,
            pairingNotSupported = 0x05u,
            encryptionKeySize = 0x06u,
            commandNotSupported = 0x07u,
            unspecifiedReason = 0x08u,
            repeatedAttempts = 0x09u,
            invalidParameters = 0x0au,
            dhKeyCheckFailed = 0x0bu,
            numericComparisonFailed = 0x0cu,
            brEdrPairingInProgress = 0x0du,
            crossTransportKeyDerivationNotAllowed = 0x0eu,
            keyRejected = 0x0fu
        };

        constexpr services::GapPairingResult ParsePairingResult(uint8_t status, uint8_t reason)
        {
            if (status == SMP_PAIRING_STATUS_SUCCESS)
                return services::GapPairingResult::success;
            if (status == SMP_PAIRING_STATUS_SMP_TIMEOUT)
                return services::GapPairingResult::timeout;
            if (status == SMP_PAIRING_STATUS_ENCRYPT_FAILED)
                return services::GapPairingResult::encryptionFailed;

            switch (static_cast<PairingFailedReason>(reason))
            {
                case PairingFailedReason::passkeyEntryFailed:
                    return services::GapPairingResult::passkeyEntryFailed;
                case PairingFailedReason::oobNotAvailable:
                    return services::GapPairingResult::oobNotAvailable;
                case PairingFailedReason::authenticationRequirements:
                    return services::GapPairingResult::authenticationRequirementsNotMet;
                case PairingFailedReason::confirmValueFailed:
                    return services::GapPairingResult::confirmValueFailed;
                case PairingFailedReason::pairingNotSupported:
                    return services::GapPairingResult::pairingNotSupported;
                case PairingFailedReason::encryptionKeySize:
                    return services::GapPairingResult::insufficientEncryptionKeySize;
                case PairingFailedReason::commandNotSupported:
                    return services::GapPairingResult::commandNotSupported;
                case PairingFailedReason::repeatedAttempts:
                    return services::GapPairingResult::repeatedAttempts;
                case PairingFailedReason::invalidParameters:
                    return services::GapPairingResult::invalidParameters;
                case PairingFailedReason::dhKeyCheckFailed:
                    return services::GapPairingResult::dhKeyCheckFailed;
                case PairingFailedReason::numericComparisonFailed:
                    return services::GapPairingResult::numericComparisonFailed;
                case PairingFailedReason::brEdrPairingInProgress:
                    return services::GapPairingResult::brEdrPairingInProgress;
                case PairingFailedReason::crossTransportKeyDerivationNotAllowed:
                    return services::GapPairingResult::crossTransportKeyDerivationNotAllowed;
                case PairingFailedReason::keyRejected:
                    return services::GapPairingResult::keyRejected;
                default:
                    return services::GapPairingResult::unknown;
            }
        }

        // The command carries out the procedure, so a controller that refuses it has refused the
        // request: the status answers the call and onDone is never reached.
        constexpr services::GapRequestStatus RequestStatusOf(tBleStatus status)
        {
            if (status == BLE_STATUS_SUCCESS)
                return services::GapRequestStatus::accepted;
            if (status == BLE_STATUS_INVALID_PARAMS)
                return services::GapRequestStatus::invalidParameter;

            return services::GapRequestStatus::invalidState;
        }
    }

    GapSt::GapSt(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration)
        : HciEventSink(hciEventSource)
        , ownAddressType(configuration.privacy ? GAP_RESOLVABLE_PRIVATE_ADDR : GAP_PUBLIC_ADDR)
        , modeAndLevel(configuration.security.modeAndLevel)
        , bondStorageSynchronizer(bondStorageSynchronizer)
    {
        connectionContext.connectionHandle = GapSt::invalidConnection;

        // HCI Reset to synchronize BLE Stack
        hci_reset();

        // Write Identity root key used to derive LTK and CSRK
        aci_hal_write_config_data(CONFIG_DATA_IR_OFFSET, CONFIG_DATA_IR_LEN, configuration.rootKeys.identity.data());

        // Write Encryption root key used to derive LTK and CSRK
        aci_hal_write_config_data(CONFIG_DATA_ER_OFFSET, CONFIG_DATA_ER_LEN, configuration.rootKeys.encryption.data());

        aci_hal_set_tx_power_level(1, configuration.txPowerLevel);
        aci_gatt_init();

        aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN, configuration.address.data());

        const std::array<uint8_t, 8> events = { { 0x9F, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
        hci_le_set_event_mask(events.data());

        SVCCTL_Init();
    }

    std::size_t GapSt::GetMaxNumberOfBonds() const
    {
        return bondStorageSynchronizer.GetMaxNumberOfBonds();
    }

    std::size_t GapSt::GetNumberOfBonds() const
    {
        uint8_t numberOfBondedAddress = 0;
        std::array<Bonded_Device_Entry_t, maxNumberOfBonds> bondedDevices;

        aci_gap_get_bonded_devices(&numberOfBondedAddress, bondedDevices.data());

        return numberOfBondedAddress;
    }

    bool GapSt::IsDeviceBonded(const services::GapAddress& address) const
    {
        return aci_gap_is_device_bonded(static_cast<uint8_t>(address.type), address.address.data()) == BLE_STATUS_SUCCESS;
    }

    std::optional<services::GapBondStrength> GapSt::BondStrength(const services::GapAddress& address) const
    {
        // ACI_GAP_GET_SECURITY_LEVEL reports the security of a link, so the strength of a bond is
        // only available while its peer is connected.
        if (connectionContext.connectionHandle == invalidConnection || address.address != connectionContext.peerAddress || address.type != connectionContext.peerAddressType)
            return std::nullopt;

        uint8_t securityMode = 0;
        uint8_t securityLevel = 0;

        if (aci_gap_get_security_level(connectionContext.connectionHandle, &securityMode, &securityLevel) != BLE_STATUS_SUCCESS)
            return std::nullopt;

        // Level 2 is unauthenticated pairing with encryption, level 3 adds authentication and level 4
        // additionally requires LE Secure Connections. The controller reports the level only, so a
        // level below 4 is reported as legacy pairing.
        // Bluetooth Core Specification, Volume 3, Part C, section 10.2.1
        return services::GapBondStrength{ securityLevel >= 4, securityLevel >= 3, static_cast<uint8_t>(securityLevel >= 2 ? encryptionKeySize : 0) };
    }

    services::GapRequestStatus GapSt::RemoveAllBonds(const infra::Function<void()>& onDone)
    {
        if (onRemoveAllBondsDone)
            return services::GapRequestStatus::busy;

        bondStorageSynchronizer.RemoveAllBonds();
        UpdateNrBonds();

        onRemoveAllBondsDone = onDone;
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                if (onRemoveAllBondsDone)
                    onRemoveAllBondsDone();
            });

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapSt::RemoveOldestBond(const infra::Function<void()>& onDone)
    {
        return services::GapRequestStatus::notSupported;
    }

    services::GapRequestStatus GapSt::PairAndBond(const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        if (connectionContext.connectionHandle == GapSt::invalidConnection)
            return services::GapRequestStatus::invalidState;

        if (onPairAndBondDone)
            return services::GapRequestStatus::busy;

        if (aci_gap_send_pairing_req(connectionContext.connectionHandle, NO_BONDING) != BLE_STATUS_SUCCESS)
            return services::GapRequestStatus::invalidState;

        onPairAndBondDone = onDone;

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapSt::SetSecurityMode(services::GapPairing::SecurityModeAndLevel modeAndLevel, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        if (modeAndLevel == services::GapPairing::SecurityModeAndLevel::mode2Level1 || modeAndLevel == services::GapPairing::SecurityModeAndLevel::mode2Level2)
            return services::GapRequestStatus::notSupported;

        if (onSetSecurityModeDone)
            return services::GapRequestStatus::busy;

        auto previous = std::exchange(this->modeAndLevel, modeAndLevel);
        auto status = RequestStatusOf(ApplyAuthenticationRequirement());

        if (status != services::GapRequestStatus::accepted)
        {
            this->modeAndLevel = previous;
            return status;
        }

        onSetSecurityModeDone = onDone;
        Complete(onSetSecurityModeDone, services::GapPairingResult::success);

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapSt::SetSecureConnectionsOnly(bool enabled, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        if (onSetSecureConnectionsOnlyDone)
            return services::GapRequestStatus::busy;

        auto previous = std::exchange(secureConnectionsOnly, enabled);
        auto status = RequestStatusOf(ApplyAuthenticationRequirement());

        if (status != services::GapRequestStatus::accepted)
        {
            secureConnectionsOnly = previous;
            return status;
        }

        onSetSecureConnectionsOnlyDone = onDone;
        Complete(onSetSecureConnectionsOnlyDone, services::GapPairingResult::success);

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapSt::SetIoCapabilities(services::GapPairing::IoCapabilities caps, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        // This port drives pairing without user interaction; the other capabilities would need the
        // passkey and numeric comparison procedures, which report notSupported below.
        if (caps != services::GapPairing::IoCapabilities::none)
            return services::GapRequestStatus::notSupported;

        if (onSetIoCapabilitiesDone)
            return services::GapRequestStatus::busy;

        // IoCapabilities carries the IO Capability values of Vol 3, Part H, section 3.3.1, which are
        // the values ACI_GAP_SET_IO_CAPABILITY takes.
        auto status = RequestStatusOf(aci_gap_set_io_capability(static_cast<uint8_t>(caps)));

        if (status != services::GapRequestStatus::accepted)
            return status;

        onSetIoCapabilitiesDone = onDone;
        Complete(onSetIoCapabilitiesDone, services::GapPairingResult::success);

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapSt::GenerateOutOfBandData(const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        if (modeAndLevel != services::GapPairing::SecurityModeAndLevel::mode1Level4)
            return services::GapRequestStatus::invalidState;

        if (onGenerateOutOfBandDataDone)
            return services::GapRequestStatus::busy;

        if (hci_le_read_local_p256_public_key() != BLE_STATUS_SUCCESS)
            return services::GapRequestStatus::invalidState;

        onGenerateOutOfBandDataDone = onDone;

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapSt::SetOutOfBandData(const services::GapOutOfBandData& outOfBandData, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        if (modeAndLevel != services::GapPairing::SecurityModeAndLevel::mode1Level4)
            return services::GapRequestStatus::invalidState;

        if (onSetOutOfBandDataDone)
            return services::GapRequestStatus::busy;

        enum OobDataType
        {
            random = 1,
            confirm
        };

        uint8_t peerAddress = outOfBandData.addressType == services::GapDeviceAddressType::publicAddress ? GAP_PUBLIC_ADDR : GAP_STATIC_RANDOM_ADDR;

        auto status = aci_gap_set_oob_data(OOB_DEVICE_TYPE_REMOTE, peerAddress, outOfBandData.macAddress.data(), static_cast<uint8_t>(OobDataType::random), static_cast<uint8_t>(outOfBandData.randomData.size()), outOfBandData.randomData.begin());

        if (status == BLE_STATUS_SUCCESS)
            status = aci_gap_set_oob_data(OOB_DEVICE_TYPE_REMOTE, peerAddress, outOfBandData.macAddress.data(), static_cast<uint8_t>(OobDataType::confirm), static_cast<uint8_t>(outOfBandData.confirmData.size()), outOfBandData.confirmData.begin());

        if (status != BLE_STATUS_SUCCESS)
            return services::GapRequestStatus::invalidParameter;

        onSetOutOfBandDataDone = onDone;
        Complete(onSetOutOfBandDataDone, services::GapPairingResult::success);

        return services::GapRequestStatus::accepted;
    }

    services::GapRequestStatus GapSt::AuthenticateWithPasskey(uint32_t passkey, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        return services::GapRequestStatus::notSupported;
    }

    services::GapRequestStatus GapSt::NumericComparisonConfirm(bool accept, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        return services::GapRequestStatus::notSupported;
    }

    GapSt::SecureConnection GapSt::SecurityModeAndLevelToSecureConnection(services::GapPairing::SecurityModeAndLevel modeAndLevel) const
    {
        return (modeAndLevel == services::GapPairing::SecurityModeAndLevel::mode1Level4) ? SecureConnection::mandatory : SecureConnection::optional;
    }

    uint8_t GapSt::SecurityModeAndLevelToMitm(services::GapPairing::SecurityModeAndLevel modeAndLevel) const
    {
        // Levels 3 and 4 are authenticated, and authentication is what MITM protection during
        // pairing provides. Asking for it without the means to satisfy it, which with these IO
        // capabilities means out of band data, fails the pairing rather than completing it with a
        // bond weaker than the level asked for.
        // Bluetooth Core Specification, Volume 3, Part C, section 10.2.1
        return modeAndLevel == services::GapPairing::SecurityModeAndLevel::mode1Level3 || modeAndLevel == services::GapPairing::SecurityModeAndLevel::mode1Level4 ? 1 : 0;
    }

    void GapSt::HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event)
    {
        really_assert(event.Connection_Handle == connectionContext.connectionHandle);

        if (event.Status == BLE_STATUS_SUCCESS)
            connectionContext.connectionHandle = GapSt::invalidConnection;
    }

    void GapSt::HandleHciLeConnectionCompleteEvent(const hci_le_connection_complete_event_rp0& event)
    {
        if (event.Status == BLE_STATUS_SUCCESS)
            SetConnectionContext(event.Connection_Handle, static_cast<services::GapDeviceAddressType>(event.Peer_Address_Type), &event.Peer_Address[0]);
    }

    void GapSt::HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event)
    {
        if (event.Status == BLE_STATUS_SUCCESS)
            SetConnectionContext(event.Connection_Handle, static_cast<services::GapDeviceAddressType>(event.Peer_Address_Type), &event.Peer_Address[0]);
    }

    void GapSt::HandleBondLostEvent()
    {
        aci_gap_allow_rebond(connectionContext.connectionHandle);
    }

    void GapSt::HandlePairingCompleteEvent(const aci_gap_pairing_complete_event_rp0& event)
    {
        really_assert(event.Connection_Handle == connectionContext.connectionHandle);

        services::GapAddress peer{ connectionContext.peerAddress, connectionContext.peerAddressType };

        if (IsDeviceBonded(peer))
        {
            hal::MacAddress address = connectionContext.peerAddress;
            aci_gap_resolve_private_addr(connectionContext.peerAddress.data(), address.data());
            bondStorageSynchronizer.UpdateBondedDevice(address);
            UpdateNrBonds();
        }

        auto result = ParsePairingResult(event.Status, event.Reason);

        if (result == services::GapPairingResult::success)
        {
            auto strength = BondStrength(peer).value_or(services::GapBondStrength{ false, false, 0 });

            GapPairing::NotifyObservers([&strength](auto& observer)
                {
                    observer.PairingSuccessfullyCompleted(strength);
                });
        }
        else
            GapPairing::NotifyObservers([result](auto& observer)
                {
                    observer.PairingFailed(result);
                });

        if (onPairAndBondDone)
            onPairAndBondDone(result);
    }

    void GapSt::HandleHciLeReadLocalP256PublicKeyCompleteEvent(const hci_le_read_local_p256_public_key_complete_event_rp0& event)
    {
        really_assert(event.Status == BLE_STATUS_SUCCESS);

        infra::EventDispatcherWithWeakPtr::Instance().Schedule([this]()
            {
                HandleOobDataGeneration();
            });
    }

    void GapSt::HandleOobDataGeneration()
    {
        uint8_t addressType = 0;
        hal::MacAddress address{};
        uint8_t dataSize = 0;
        std::array<uint8_t, 16> randomData{};
        std::array<uint8_t, 16> confirmData{};

        /* OOB data generation */
        aci_gap_set_oob_data(0x00, 0x00, nullptr, 0x00, 0x00, nullptr);

        /* OOB data recovery */
        auto status = aci_gap_get_oob_data(0x01, &addressType, address.data(), &dataSize, randomData.data());
        really_assert(status == BLE_STATUS_SUCCESS && dataSize == randomData.size());
        status = aci_gap_get_oob_data(0x02, &addressType, address.data(), &dataSize, confirmData.data());
        really_assert(status == BLE_STATUS_SUCCESS && dataSize == confirmData.size());

        auto addressTypeConverted = addressType == GAP_PUBLIC_ADDR ? services::GapDeviceAddressType::publicAddress : services::GapDeviceAddressType::randomAddress;
        services::GapOutOfBandData outOfBandData = { address, addressTypeConverted, infra::MakeConstByteRange(randomData), infra::MakeConstByteRange(confirmData) };

        infra::Subject<services::GapPairingObserver>::NotifyObservers([outOfBandData](auto& observer)
            {
                observer.OutOfBandDataGenerated(outOfBandData);
            });

        if (onGenerateOutOfBandDataDone)
            onGenerateOutOfBandDataDone(services::GapPairingResult::success);
    }

    tBleStatus GapSt::SetAddress(const hal::MacAddress& address, services::GapDeviceAddressType addressType) const
    {
        uint8_t offset = addressType == services::GapDeviceAddressType::publicAddress ? CONFIG_DATA_PUBADDR_OFFSET : CONFIG_DATA_RANDOM_ADDRESS_OFFSET;
        uint8_t length = addressType == services::GapDeviceAddressType::publicAddress ? CONFIG_DATA_PUBADDR_LEN : CONFIG_DATA_RANDOM_ADDRESS_LEN;

        return aci_hal_write_config_data(offset, length, address.data());
    }

    tBleStatus GapSt::ApplyAuthenticationRequirement() const
    {
        auto secureConnection = secureConnectionsOnly ? SecureConnection::mandatory : SecurityModeAndLevelToSecureConnection(modeAndLevel);

        return aci_gap_set_authentication_requirement(bondingMode, SecurityModeAndLevelToMitm(modeAndLevel), static_cast<uint8_t>(secureConnection), keypressNotificationSupport, encryptionKeySize, encryptionKeySize, 0, 111111, GAP_PUBLIC_ADDR);
    }

    void GapSt::HciEvent(hci_event_pckt& event)
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

    void GapSt::HandleHciLeMetaEvent(const evt_le_meta_event& metaEvent)
    {
        switch (metaEvent.subevent)
        {
            case HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE:
                HandleHciLeConnectionCompleteEvent(*reinterpret_cast<const hci_le_connection_complete_event_rp0*>(metaEvent.data));
                break;
            case HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE:
                HandleHciLeAdvertisingReportEvent(*reinterpret_cast<const hci_le_advertising_report_event_rp0*>(metaEvent.data));
                break;
            case HCI_LE_CONNECTION_UPDATE_COMPLETE_SUBEVT_CODE:
                HandleHciLeConnectionUpdateCompleteEvent(*reinterpret_cast<const hci_le_connection_update_complete_event_rp0*>(metaEvent.data));
                break;
            case HCI_LE_DATA_LENGTH_CHANGE_SUBEVT_CODE:
                HandleHciLeDataLengthChangeEvent(*reinterpret_cast<const hci_le_data_length_change_event_rp0*>(metaEvent.data));
                break;
            case HCI_LE_PHY_UPDATE_COMPLETE_SUBEVT_CODE:
                HandleHciLePhyUpdateCompleteEvent(*reinterpret_cast<const hci_le_phy_update_complete_event_rp0*>(metaEvent.data));
                break;
            case HCI_LE_ENHANCED_CONNECTION_COMPLETE_SUBEVT_CODE:
                HandleHciLeEnhancedConnectionCompleteEvent(*reinterpret_cast<const hci_le_enhanced_connection_complete_event_rp0*>(metaEvent.data));
                break;
            case HCI_LE_READ_LOCAL_P256_PUBLIC_KEY_COMPLETE_SUBEVT_CODE:
                HandleHciLeReadLocalP256PublicKeyCompleteEvent(*reinterpret_cast<const hci_le_read_local_p256_public_key_complete_event_rp0*>(metaEvent.data));
                break;
            default:
                break;
        }
    }

    void GapSt::HandleHciVendorSpecificDebugEvent(const evt_blecore_aci& event)
    {
        switch (event.ecode)
        {
            case ACI_GAP_PAIRING_COMPLETE_VSEVT_CODE:
                HandlePairingCompleteEvent(*reinterpret_cast<const aci_gap_pairing_complete_event_rp0*>(event.data));
                break;
            case ACI_GAP_BOND_LOST_VSEVT_CODE:
                HandleBondLostEvent();
                break;
            case ACI_GAP_PROC_COMPLETE_VSEVT_CODE:
                HandleGapProcedureCompleteEvent(*reinterpret_cast<const aci_gap_proc_complete_event_rp0*>(event.data));
                break;
            case ACI_GATT_PROC_COMPLETE_VSEVT_CODE:
                HandleGattCompleteEvent(*reinterpret_cast<const aci_gatt_proc_complete_event_rp0*>(event.data));
                break;
            case ACI_L2CAP_CONNECTION_UPDATE_REQ_VSEVT_CODE:
                HandleL2capConnectionUpdateRequestEvent(*reinterpret_cast<const aci_l2cap_connection_update_req_event_rp0*>(event.data));
                break;
            case ACI_L2CAP_CONNECTION_UPDATE_RESP_VSEVT_CODE:
                HandleL2capConnectionUpdateResponseEvent(*reinterpret_cast<const aci_l2cap_connection_update_resp_event_rp0*>(event.data));
                break;
            default:
                break;
        }
    }

    void GapSt::SetConnectionContext(uint16_t connectionHandle, services::GapDeviceAddressType peerAddressType, const uint8_t* peerAddress)
    {
        connectionContext.connectionHandle = connectionHandle;
        connectionContext.peerAddressType = peerAddressType;
        std::copy_n(peerAddress, connectionContext.peerAddress.size(), std::begin(connectionContext.peerAddress));
    }

    void GapSt::UpdateNrBonds()
    {
        auto nrBonds = this->GetNumberOfBonds();
        services::GapBonding::NotifyObservers([nrBonds](auto& obs)
            {
                obs.NumberOfBondsChanged(nrBonds);
            });
    }
}
