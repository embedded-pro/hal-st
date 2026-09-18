#ifndef HAL_ST_TRACING_GAP_CENTRAL_ST_HPP
#define HAL_ST_TRACING_GAP_CENTRAL_ST_HPP

#include "hal_st/middlewares/ble_middleware/GapCentralSt.hpp"
#include "services/tracer/Tracer.hpp"

namespace hal
{
    class TracingGapCentralSt
        : public GapCentralSt
    {
    public:
        TracingGapCentralSt(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration, services::Tracer& tracer);

        // Implementation of services::GapCentral
        using GapCentralSt::Connect;
        services::GapRequestStatus Connect(const services::GapAddress& peer, const services::GapConnectionParameters& parameters, infra::Duration initiatingTimeout, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus UpdateConnectionParameters(const services::GapConnectionParameters& parameters, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus CancelConnect(const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus Disconnect(const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus SetAddress(const services::GapAddress& address, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus SetDataLength(const services::GapDataLength& dataLength) override;
        services::GapRequestStatus SetPhy(services::GapPhy txPhy, services::GapPhy rxPhy) override;
        using GapCentralSt::StartDeviceDiscovery;
        services::GapRequestStatus StartDeviceDiscovery(const services::GapScanParameters& parameters, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus StopDeviceDiscovery(const infra::Function<void(Result)>& onDone) override;
        std::optional<services::GapAddress> ResolvePrivateAddress(hal::MacAddress address) const override;

        // Implementation of GapBonding
        std::size_t GetMaxNumberOfBonds() const override;
        std::size_t GetNumberOfBonds() const override;
        bool IsDeviceBonded(const services::GapAddress& address) const override;
        std::optional<services::GapBondStrength> BondStrength(const services::GapAddress& address) const override;
        services::GapRequestStatus RemoveAllBonds(const infra::Function<void()>& onDone) override;
        services::GapRequestStatus RemoveOldestBond(const infra::Function<void()>& onDone) override;

        // Implementation of GapPairing
        services::GapRequestStatus PairAndBond(const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus SetSecurityMode(services::GapPairing::SecurityModeAndLevel modeAndLevel, const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus SetSecureConnectionsOnly(bool enabled, const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus SetIoCapabilities(services::GapPairing::IoCapabilities caps, const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus AuthenticateWithPasskey(uint32_t passkey, const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus NumericComparisonConfirm(bool accept, const infra::Function<void(services::GapPairingResult)>& onDone) override;

    protected:
        // Implementation of GapCentralSt
        void HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event) override;
        void HandleHciLeConnectionCompleteEvent(const hci_le_connection_complete_event_rp0& event) override;
        void HandleHciLeConnectionUpdateCompleteEvent(const hci_le_connection_update_complete_event_rp0& event) override;
        void HandleHciLeDataLengthChangeEvent(const hci_le_data_length_change_event_rp0& event) override;
        void HandleHciLePhyUpdateCompleteEvent(const hci_le_phy_update_complete_event_rp0& event) override;
        void HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event) override;
        void HandleGapProcedureCompleteEvent(const aci_gap_proc_complete_event_rp0& event) override;
        void HandleL2capConnectionUpdateRequestEvent(const aci_l2cap_connection_update_req_event_rp0& event) override;
        void HandleL2capConnectionUpdateResponseEvent(const aci_l2cap_connection_update_resp_event_rp0& event) override;
        void HandlePairingCompleteEvent(const aci_gap_pairing_complete_event_rp0& event) override;

    private:
        void TraceRequest(infra::BoundedConstString procedure, services::GapRequestStatus status) const;

    private:
        services::Tracer& tracer;

        // The caller's callback does not fit in the storage of a Function capturing it, so the two
        // procedures whose outcome is traced keep it here and hand down a callback capturing only
        // this.
        infra::AutoResetFunction<void(Result), sizeof(infra::Function<void(Result)>)> onConnectDone;
        infra::AutoResetFunction<void(services::GapPairingResult), sizeof(infra::Function<void(services::GapPairingResult)>)> onPairAndBondDone;
    };
}

namespace infra
{
    TextOutputStream& operator<<(TextOutputStream& stream, const services::GapRequestStatus& status);
    TextOutputStream& operator<<(TextOutputStream& stream, const services::GapPairingResult& result);
    TextOutputStream& operator<<(TextOutputStream& stream, const services::GapCentral::Result& result);
}

#endif
