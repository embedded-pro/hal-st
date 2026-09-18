#ifndef HAL_ST_GAP_CENTRAL_ST_HPP
#define HAL_ST_GAP_CENTRAL_ST_HPP

#include "ble/ble.h"
#include "hal_st/middlewares/ble_middleware/GapSt.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/ble/GapCentral.hpp"

namespace hal
{
    class GapCentralSt
        : public services::GapCentral
        , public GapSt
    {
    public:
        GapCentralSt(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration);

        // Implementation of services::GapCentral
        std::optional<services::GapAddress> ResolvePrivateAddress(hal::MacAddress address) const override;
        using services::GapCentral::Connect;
        services::GapRequestStatus Connect(const services::GapAddress& peer, const services::GapConnectionParameters& parameters, infra::Duration initiatingTimeout, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus UpdateConnectionParameters(const services::GapConnectionParameters& parameters, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus CancelConnect(const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus Disconnect(const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus SetAddress(const services::GapAddress& address, const infra::Function<void(Result)>& onDone) override;
        using services::GapCentral::StartDeviceDiscovery;
        services::GapRequestStatus StartDeviceDiscovery(const services::GapScanParameters& parameters, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus StopDeviceDiscovery(const infra::Function<void(Result)>& onDone) override;

        // Implementation of GapPairing
        services::GapRequestStatus AllowPairing(bool allow, const infra::Function<void(services::GapPairingResult)>& onDone) override;

    protected:
        [[nodiscard]] SecureConnection SecurityModeAndLevelToSecureConnection(services::GapPairing::SecurityModeAndLevel modeAndLevel) const override;

        void HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event) override;
        void HandleHciLeAdvertisingReportEvent(const hci_le_advertising_report_event_rp0& event) override;
        void HandleHciLeConnectionCompleteEvent(const hci_le_connection_complete_event_rp0& event) override;
        void HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event) override;
        void HandleHciLeConnectionUpdateCompleteEvent(const hci_le_connection_update_complete_event_rp0& event) override;
        void HandleHciLeDataLengthChangeEvent(const hci_le_data_length_change_event_rp0& event) override;
        void HandleHciLePhyUpdateCompleteEvent(const hci_le_phy_update_complete_event_rp0& event) override;
        void HandleGapProcedureCompleteEvent(const aci_gap_proc_complete_event_rp0& event) override;
        void HandleGattCompleteEvent(const aci_gatt_proc_complete_event_rp0& event) override;
        void HandleL2capConnectionUpdateRequestEvent(const aci_l2cap_connection_update_req_event_rp0& event) override;

    private:
        using CentralCompletion = infra::AutoResetFunction<void(Result)>;

        void HandleGapDiscoveryProcedureEvent(uint8_t status);
        void HandleGapDirectConnectionProcedureCompleteEvent();

        void HandleAdvertisingReport(const Advertising_Report_t& advertisingReport);
        void SetDataLength();
        void UpdateStateOnConnectionComplete(uint8_t status);
        void HandleConnectionCompleteCommon(uint8_t status);
        void Initialize(const Configuration& configuration);

    private:
        // Create connection parameters
        const uint16_t leScanInterval = 0x320;
        const uint16_t leScanWindow = 0x320;

        // Terminate connection
        const uint8_t remoteUserTerminatedConnection = 0x13;

        // HCI status
        const uint8_t commandDisallowed = 0x0c;

        bool discovering = false;
        services::GapConnectionParameters connectionParameters;

        // Why an initiating procedure that ends without a connection ended.
        Result connectFailureResult = Result::connectionFailed;

        infra::TimerSingleShot initiatingStateTimer;

        CentralCompletion onConnectDone;
        CentralCompletion onCancelConnectDone;
        CentralCompletion onDisconnectDone;
        CentralCompletion onSetAddressDone;
        CentralCompletion onStartDeviceDiscoveryDone;
        CentralCompletion onStopDeviceDiscoveryDone;
        CentralCompletion onUpdateConnectionParametersDone;
    };
}

#endif
