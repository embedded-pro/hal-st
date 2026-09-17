#ifndef HAL_ST_GAP_PERIPHERAL_ST_HPP
#define HAL_ST_GAP_PERIPHERAL_ST_HPP

#include "hal_st/middlewares/ble_middleware/GapSt.hpp"
#include "infra/util/BoundedVector.hpp"
#include "services/ble/GapPeripheral.hpp"

namespace hal
{
    class GapPeripheralSt
        : public services::GapPeripheral
        , public GapSt
    {
    public:
        GapPeripheralSt(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration);

        // Implementation of GapPeripheral
        services::GapAddress GetAddress() const override;
        services::GapAddress GetIdentityAddress() const override;
        infra::ConstByteRange GetAdvertisementData() const override;
        infra::ConstByteRange GetScanResponseData() const override;
        services::GapRequestStatus SetAdvertisementData(infra::ConstByteRange data, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus SetScanResponseData(infra::ConstByteRange data, const infra::Function<void(Result)>& onDone) override;
        using services::GapPeripheral::Advertise;
        services::GapRequestStatus Advertise(const services::GapAdvertisingParameters& parameters, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus AdvertiseDirected(services::GapDirectedAdvertisementType type, const services::GapAddress& peer, AdvertisementIntervalMultiplier multiplier, const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus Standby(const infra::Function<void(Result)>& onDone) override;
        services::GapRequestStatus RequestConnectionParameterUpdate(const services::GapConnectionParameters& connParam, const infra::Function<void(Result)>& onDone) override;

        // Implementation of GapPairing
        services::GapRequestStatus AllowPairing(bool allow, const infra::Function<void(services::GapPairingResult)>& onDone) override;

    protected:
        // Implementation of GapSt
        void HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event) override;
        void HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event) override;
        void HandleL2capConnectionUpdateResponseEvent(const aci_l2cap_connection_update_resp_event_rp0& event) override;

    private:
        using PeripheralCompletion = infra::AutoResetFunction<void(Result)>;

        void UpdateAdvertisementData();
        void UpdateState(services::GapPeripheralState newstate);
        void UpdateResolvingList();
        void ClearResolvingList();
        void Initialize(const Configuration& configuration);

    private:
        services::GapPeripheralState state = services::GapPeripheralState::standby;
        bool allowPairing = true;

        infra::BoundedVector<uint8_t>::WithMaxSize<services::gapMaxAdvertisementDataSize> advertisementData;
        infra::BoundedVector<uint8_t>::WithMaxSize<services::gapMaxScanResponseDataSize> scanResponseData;

        PeripheralCompletion onSetAdvertisementDataDone;
        PeripheralCompletion onSetScanResponseDataDone;
        PeripheralCompletion onAdvertiseDone;
        PeripheralCompletion onAdvertiseDirectedDone;
        PeripheralCompletion onStandbyDone;
        PeripheralCompletion onRequestConnectionParameterUpdateDone;
        PairingCompletion onAllowPairingDone;

        virtual void StartedAdvertising(infra::BoundedConstString functionName) {};
        virtual void ReceivedNumberOfBondedAddresses(uint8_t numberOfBondedAddresses) {};

        const Whitelist_Identity_Entry_t dummyPeer{ 0x01, { 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF } };
    };
}

#endif
