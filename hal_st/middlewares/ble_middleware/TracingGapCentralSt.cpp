#include "hal_st/middlewares/ble_middleware/TracingGapCentralSt.hpp"

namespace infra
{
    TextOutputStream& operator<<(TextOutputStream& stream, const services::GapRequestStatus& status)
    {
        switch (status)
        {
            case services::GapRequestStatus::accepted:
                return stream << "accepted";
            case services::GapRequestStatus::invalidState:
                return stream << "invalidState";
            case services::GapRequestStatus::invalidParameter:
                return stream << "invalidParameter";
            case services::GapRequestStatus::busy:
                return stream << "busy";
            default:
                return stream << "notSupported";
        }
    }

    TextOutputStream& operator<<(TextOutputStream& stream, const services::GapPairingResult& result)
    {
        return stream << "0x" << infra::hex << infra::enum_cast(result);
    }

    TextOutputStream& operator<<(TextOutputStream& stream, const services::GapCentral::Result& result)
    {
        switch (result)
        {
            case services::GapCentral::Result::success:
                return stream << "success";
            case services::GapCentral::Result::cancelled:
                return stream << "cancelled";
            case services::GapCentral::Result::timeout:
                return stream << "timeout";
            case services::GapCentral::Result::connectionFailed:
                return stream << "connectionFailed";
            default:
                return stream << "controllerError";
        }
    }
}

namespace hal
{
    TracingGapCentralSt::TracingGapCentralSt(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration, services::Tracer& tracer)
        : GapCentralSt(hciEventSource, bondStorageSynchronizer, configuration)
        , tracer(tracer)
    {}

    void TracingGapCentralSt::TraceRequest(infra::BoundedConstString procedure, services::GapRequestStatus status) const
    {
        tracer.Trace() << "TracingGapCentralSt::" << procedure << " -> " << status;
    }

    services::GapRequestStatus TracingGapCentralSt::Connect(const services::GapAddress& peer, const services::GapConnectionParameters& parameters, infra::Duration initiatingTimeout, const infra::Function<void(Result)>& onDone)
    {
        tracer.Trace() << "TracingGapCentralSt::Connect, MAC address: "
                       << infra::AsMacAddress(peer.address)
                       << ", type: "
                       << peer.type
                       << ", initiating timeout (ms): "
                       << std::chrono::duration_cast<std::chrono::milliseconds>(initiatingTimeout).count();

        onConnectDone = onDone;

        auto status = GapCentralSt::Connect(peer, parameters, initiatingTimeout, [this](Result result)
            {
                tracer.Trace() << "TracingGapCentralSt::Connect done -> " << result;

                if (onConnectDone)
                    onConnectDone(result);
            });

        if (status != services::GapRequestStatus::accepted)
            onConnectDone = nullptr;

        TraceRequest("Connect", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::UpdateConnectionParameters(const services::GapConnectionParameters& parameters, const infra::Function<void(Result)>& onDone)
    {
        tracer.Trace() << "TracingGapCentralSt::UpdateConnectionParameters";
        tracer.Trace() << "\tInterval            : " << parameters.minConnectionInterval << " .. " << parameters.maxConnectionInterval;
        tracer.Trace() << "\tPeripheral latency  : " << parameters.peripheralLatency;
        tracer.Trace() << "\tSupervision timeout : " << parameters.supervisionTimeout;

        auto status = GapCentralSt::UpdateConnectionParameters(parameters, onDone);
        TraceRequest("UpdateConnectionParameters", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::CancelConnect(const infra::Function<void(Result)>& onDone)
    {
        auto status = GapCentralSt::CancelConnect(onDone);
        TraceRequest("CancelConnect", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::Disconnect(const infra::Function<void(Result)>& onDone)
    {
        auto status = GapCentralSt::Disconnect(onDone);
        TraceRequest("Disconnect", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::SetAddress(const services::GapAddress& address, const infra::Function<void(Result)>& onDone)
    {
        tracer.Trace() << "TracingGapCentralSt::SetAddress, MAC address: "
                       << infra::AsMacAddress(address.address)
                       << ", type: "
                       << address.type;

        auto status = GapCentralSt::SetAddress(address, onDone);
        TraceRequest("SetAddress", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::StartDeviceDiscovery(const services::GapScanParameters& parameters, const infra::Function<void(Result)>& onDone)
    {
        tracer.Trace() << "TracingGapCentralSt::StartDeviceDiscovery, interval: " << parameters.interval << ", window: " << parameters.window;

        auto status = GapCentralSt::StartDeviceDiscovery(parameters, onDone);
        TraceRequest("StartDeviceDiscovery", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::StopDeviceDiscovery(const infra::Function<void(Result)>& onDone)
    {
        auto status = GapCentralSt::StopDeviceDiscovery(onDone);
        TraceRequest("StopDeviceDiscovery", status);

        return status;
    }

    std::optional<services::GapAddress> TracingGapCentralSt::ResolvePrivateAddress(hal::MacAddress address) const
    {
        auto resolved = GapCentralSt::ResolvePrivateAddress(address);
        tracer.Trace() << "TracingGapCentralSt::ResolvePrivateAddress, MAC address: " << infra::AsMacAddress(address);

        if (resolved)
            tracer.Continue() << ", resolved MAC address " << infra::AsMacAddress(resolved->address) << " type " << resolved->type;
        else
            tracer.Continue() << ", could not resolve MAC address";

        return resolved;
    }

    std::size_t TracingGapCentralSt::GetMaxNumberOfBonds() const
    {
        tracer.Trace() << "TracingGapCentralSt::GetMaxNumberOfBonds";
        return GapCentralSt::GetMaxNumberOfBonds();
    }

    std::size_t TracingGapCentralSt::GetNumberOfBonds() const
    {
        tracer.Trace() << "TracingGapCentralSt::GetNumberOfBonds";
        return GapCentralSt::GetNumberOfBonds();
    }

    bool TracingGapCentralSt::IsDeviceBonded(const services::GapAddress& address) const
    {
        auto ret = GapCentralSt::IsDeviceBonded(address);
        tracer.Trace() << "TracingGapCentralSt::IsDeviceBonded " << infra::AsMacAddress(address.address) << " -> " << (ret ? "true" : "false");

        return ret;
    }

    std::optional<services::GapBondStrength> TracingGapCentralSt::BondStrength(const services::GapAddress& address) const
    {
        auto strength = GapCentralSt::BondStrength(address);
        tracer.Trace() << "TracingGapCentralSt::BondStrength " << infra::AsMacAddress(address.address);

        if (strength)
            tracer.Continue() << " -> secure connections " << (strength->secureConnections ? "yes" : "no")
                              << ", authenticated " << (strength->authenticated ? "yes" : "no")
                              << ", key size " << strength->encryptionKeySize;
        else
            tracer.Continue() << " -> unknown";

        return strength;
    }

    services::GapRequestStatus TracingGapCentralSt::RemoveAllBonds(const infra::Function<void()>& onDone)
    {
        auto status = GapCentralSt::RemoveAllBonds(onDone);
        TraceRequest("RemoveAllBonds", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::RemoveOldestBond(const infra::Function<void()>& onDone)
    {
        auto status = GapCentralSt::RemoveOldestBond(onDone);
        TraceRequest("RemoveOldestBond", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::PairAndBond(const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        onPairAndBondDone = onDone;

        auto status = GapCentralSt::PairAndBond([this](services::GapPairingResult result)
            {
                tracer.Trace() << "TracingGapCentralSt::PairAndBond done -> " << result;

                if (onPairAndBondDone)
                    onPairAndBondDone(result);
            });

        if (status != services::GapRequestStatus::accepted)
            onPairAndBondDone = nullptr;

        TraceRequest("PairAndBond", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::SetSecurityMode(services::GapPairing::SecurityModeAndLevel modeAndLevel, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        auto status = GapCentralSt::SetSecurityMode(modeAndLevel, onDone);
        TraceRequest("SetSecurityMode", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::SetSecureConnectionsOnly(bool enabled, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        auto status = GapCentralSt::SetSecureConnectionsOnly(enabled, onDone);
        TraceRequest("SetSecureConnectionsOnly", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::SetIoCapabilities(services::GapPairing::IoCapabilities caps, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        auto status = GapCentralSt::SetIoCapabilities(caps, onDone);
        TraceRequest("SetIoCapabilities", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::AuthenticateWithPasskey(uint32_t passkey, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        auto status = GapCentralSt::AuthenticateWithPasskey(passkey, onDone);
        TraceRequest("AuthenticateWithPasskey", status);

        return status;
    }

    services::GapRequestStatus TracingGapCentralSt::NumericComparisonConfirm(bool accept, const infra::Function<void(services::GapPairingResult)>& onDone)
    {
        auto status = GapCentralSt::NumericComparisonConfirm(accept, onDone);
        TraceRequest("NumericComparisonConfirm", status);

        return status;
    }

    void TracingGapCentralSt::HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event)
    {
        tracer.Trace() << "TracingGapCentralSt::HandleHciDisconnectEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tReason              : 0x" << infra::hex << event.Reason;

        GapCentralSt::HandleHciDisconnectEvent(event);
    }

    void TracingGapCentralSt::HandleHciLeConnectionCompleteEvent(const hci_le_connection_complete_event_rp0& event)
    {
        hal::MacAddress mac;
        infra::Copy(infra::MakeRange(event.Peer_Address), infra::MakeRange(mac));

        tracer.Trace() << "TracingGapCentralSt::HandleHciLeConnectionCompleteEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tPeer address        : " << infra::AsMacAddress(mac);
        tracer.Trace() << "\tPeer address type   : " << event.Peer_Address_Type;

        GapCentralSt::HandleHciLeConnectionCompleteEvent(event);
    }

    void TracingGapCentralSt::HandleHciLeConnectionUpdateCompleteEvent(const hci_le_connection_update_complete_event_rp0& event)
    {
        tracer.Trace() << "TracingGapCentralSt::HandleHciLeConnectionUpdateCompleteEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tConnection Interval : " << event.Conn_Interval;
        tracer.Trace() << "\tConnection Latency  : " << event.Conn_Latency;
        tracer.Trace() << "\tSupervision Timeout : " << event.Supervision_Timeout;

        GapCentralSt::HandleHciLeConnectionUpdateCompleteEvent(event);
    }

    void TracingGapCentralSt::HandleHciLeDataLengthChangeEvent(const hci_le_data_length_change_event_rp0& event)
    {
        tracer.Trace() << "TracingGapCentralSt::HandleHciLeDataLengthChangeEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tMax TX octets       : " << event.MaxTxOctets;
        tracer.Trace() << "\tMax TX time         : " << event.MaxTxTime;
        tracer.Trace() << "\tMax RX octets       : " << event.MaxRxOctets;
        tracer.Trace() << "\tMax RX time         : " << event.MaxRxTime;
        GapCentralSt::HandleHciLeDataLengthChangeEvent(event);
    }

    void TracingGapCentralSt::HandleHciLePhyUpdateCompleteEvent(const hci_le_phy_update_complete_event_rp0& event)
    {
        tracer.Trace() << "TracingGapCentralSt::HandleHciLePhyUpdateCompleteEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tRX phy              : " << event.RX_PHY << " Mbps";
        tracer.Trace() << "\tTX phy              : " << event.TX_PHY << " Mbps";

        GapCentralSt::HandleHciLePhyUpdateCompleteEvent(event);
    }

    void TracingGapCentralSt::HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event)
    {
        hal::MacAddress mac;
        infra::Copy(infra::MakeRange(event.Peer_Address), infra::MakeRange(mac));

        tracer.Trace() << "TracingGapCentralSt::HandleHciLeEnhancedConnectionCompleteEvent Handle";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tPeer address        : " << infra::AsMacAddress(mac);
        tracer.Trace() << "\tPeer address type   : " << event.Peer_Address_Type;

        GapCentralSt::HandleHciLeEnhancedConnectionCompleteEvent(event);
    }

    void TracingGapCentralSt::HandleGapProcedureCompleteEvent(const aci_gap_proc_complete_event_rp0& event)
    {
        if (event.Procedure_Code == GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC)
            tracer.Trace() << "TracingGapCentralSt::HandleGapProcedureCompleteEvent Direct Connection establishment status: 0x" << infra::hex << event.Status;

        GapCentralSt::HandleGapProcedureCompleteEvent(event);
    }

    void TracingGapCentralSt::HandleL2capConnectionUpdateRequestEvent(const aci_l2cap_connection_update_req_event_rp0& event)
    {
#if defined(STM32WB)
        auto latency = event.Slave_Latency;
#else
        auto latency = event.Latency;
#endif

        tracer.Trace() << "TracingGapCentralSt::HandleL2capConnectionUpdateRequestEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tIdentifier          : 0x" << infra::hex << event.Identifier;
        tracer.Trace() << "\tL2CAP length        : " << event.L2CAP_Length;
        tracer.Trace() << "\tInterval min        : " << event.Interval_Min;
        tracer.Trace() << "\tInterval max        : " << event.Interval_Max;
        tracer.Trace() << "\tPeripheral latency  : " << latency;

        GapCentralSt::HandleL2capConnectionUpdateRequestEvent(event);
    }

    void TracingGapCentralSt::HandleL2capConnectionUpdateResponseEvent(const aci_l2cap_connection_update_resp_event_rp0& event)
    {
        tracer.Trace() << "TracingGapCentralSt::HandleL2capConnectionUpdateResponseEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tResult              : 0x" << infra::hex << event.Result;

        GapCentralSt::HandleL2capConnectionUpdateResponseEvent(event);
    }

    void TracingGapCentralSt::HandlePairingCompleteEvent(const aci_gap_pairing_complete_event_rp0& event)
    {
        tracer.Trace() << "TracingGapCentralSt::HandlePairingCompleteEvent";
        tracer.Trace() << "\tConnection handle   : 0x" << infra::hex << event.Connection_Handle;
        tracer.Trace() << "\tStatus              : 0x" << infra::hex << event.Status;
        tracer.Trace() << "\tReason              : 0x" << infra::hex << event.Reason;
        GapCentralSt::HandlePairingCompleteEvent(event);
    }
}
