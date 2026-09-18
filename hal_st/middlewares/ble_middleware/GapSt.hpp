#ifndef HAL_ST_GAP_ST_HPP
#define HAL_ST_GAP_ST_HPP

#include "ble/ble.h"
#include "ble_defs.h"
#include "hal_st/middlewares/ble_middleware/HciEventObserver.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "services/ble/BondStorageSynchronizer.hpp"
#include "services/ble/GapBonding.hpp"
#include "services/ble/GapPairing.hpp"
#include "services/ble/GattTypes.hpp"
#include <optional>

namespace hal
{
    class GapSt
        : public services::GapBonding
        , public services::GapPairing
        , private HciEventSink
    {
    public:
        struct GapService
        {
            infra::BoundedString::WithStorage<32> deviceName;
            uint16_t appearance;
        };

        struct RootKeys
        {
            std::array<uint8_t, 16> identity;
            std::array<uint8_t, 16> encryption;
        };

        struct Security
        {
            services::GapPairing::IoCapabilities ioCapabilities;
            services::GapPairing::SecurityModeAndLevel modeAndLevel;
        };

        static constexpr Security justWorks{ services::GapPairing::IoCapabilities::none, services::GapPairing::SecurityModeAndLevel::mode1Level1 };
        static constexpr Security encrypted{ services::GapPairing::IoCapabilities::none, services::GapPairing::SecurityModeAndLevel::mode1Level2 };
        static constexpr Security outOfBand{ services::GapPairing::IoCapabilities::none, services::GapPairing::SecurityModeAndLevel::mode1Level4 };

        struct Configuration
        {
            const MacAddress& address;
            const GapService& gapService;
            const RootKeys& rootKeys;
            const Security& security;
            uint8_t txPowerLevel;
            bool privacy;
        };

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
        services::GapRequestStatus GenerateOutOfBandData(const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus SetOutOfBandData(const services::GapOutOfBandData& outOfBandData, const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus AuthenticateWithPasskey(uint32_t passkey, const infra::Function<void(services::GapPairingResult)>& onDone) override;
        services::GapRequestStatus NumericComparisonConfirm(bool accept, const infra::Function<void(services::GapPairingResult)>& onDone) override;

    protected:
        enum class SecureConnection : uint8_t
        {
            notSupported = 0,
            optional = 1,
            mandatory
        };

        using PairingCompletion = infra::AutoResetFunction<void(services::GapPairingResult)>;

        GapSt(HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer, const Configuration& configuration);

        virtual void HandleHciDisconnectEvent(const hci_disconnection_complete_event_rp0& event);

        virtual void HandleHciLeConnectionCompleteEvent(const hci_le_connection_complete_event_rp0& event);
        virtual void HandleHciLeAdvertisingReportEvent(const hci_le_advertising_report_event_rp0& event) {};
        virtual void HandleHciLeConnectionUpdateCompleteEvent(const hci_le_connection_update_complete_event_rp0& event) {};
        virtual void HandleHciLeDataLengthChangeEvent(const hci_le_data_length_change_event_rp0& event) {};
        virtual void HandleHciLePhyUpdateCompleteEvent(const hci_le_phy_update_complete_event_rp0& event) {};
        virtual void HandleHciLeEnhancedConnectionCompleteEvent(const hci_le_enhanced_connection_complete_event_rp0& event);
        virtual void HandleHciLeReadLocalP256PublicKeyCompleteEvent(const hci_le_read_local_p256_public_key_complete_event_rp0& event);

        virtual void HandlePairingCompleteEvent(const aci_gap_pairing_complete_event_rp0& event);
        virtual void HandleBondLostEvent();
        virtual void HandleGapProcedureCompleteEvent(const aci_gap_proc_complete_event_rp0& event) {};
        virtual void HandleGattCompleteEvent(const aci_gatt_proc_complete_event_rp0& event) {};
        virtual void HandleL2capConnectionUpdateRequestEvent(const aci_l2cap_connection_update_req_event_rp0& event) {};
        virtual void HandleL2capConnectionUpdateResponseEvent(const aci_l2cap_connection_update_resp_event_rp0& event) {};

        [[nodiscard]] virtual SecureConnection SecurityModeAndLevelToSecureConnection(services::GapPairing::SecurityModeAndLevel modeAndLevel) const;
        [[nodiscard]] virtual uint8_t SecurityModeAndLevelToMitm(services::GapPairing::SecurityModeAndLevel modeAndLevel) const;

        tBleStatus SetAddress(const MacAddress& address, services::GapDeviceAddressType addressType) const;

        template<class Completion, class Result>
        static void Complete(Completion& completion, Result result)
        {
            infra::EventDispatcher::Instance().Schedule([&completion, result]()
                {
                    if (completion)
                        completion(result);
                });
        }

    private:
        // Implementation of HciEventSink
        void HciEvent(hci_event_pckt& event) override;

        void HandleHciLeMetaEvent(const evt_le_meta_event& metaEvent);
        void HandleHciVendorSpecificDebugEvent(const evt_blecore_aci& event);
        void HandleOobDataGeneration();

        void SetConnectionContext(uint16_t connectionHandle, services::GapDeviceAddressType peerAddressType, const uint8_t* peerAddress);
        void UpdateNrBonds();

        tBleStatus ApplyAuthenticationRequirement() const;

    protected:
        struct ConnectionContext
        {
            uint16_t connectionHandle;
            services::GapDeviceAddressType peerAddressType;
            MacAddress peerAddress;
        };

        ConnectionContext connectionContext;
        uint8_t ownAddressType;
        services::GapPairing::SecurityModeAndLevel modeAndLevel;

        const uint16_t invalidConnection = 0xffff;

        const uint8_t allPhys = 0;
        const uint8_t speed1Mbps = 0x1;
        const uint8_t speed2Mbps = 0x2;

        const uint8_t ioCapability = IO_CAP_NO_INPUT_NO_OUTPUT;
        const uint8_t bondingMode = BONDING;
        const uint8_t keypressNotificationSupport = KEYPRESS_SUPPORTED;
        static constexpr uint8_t maxNumberOfBonds = 10;

        // Both bounds are set to the maximum, so a completed pairing always has a 128 bit key.
        static constexpr uint8_t encryptionKeySize = 16;

    private:
        services::BondStorageSynchronizer& bondStorageSynchronizer;
        bool secureConnectionsOnly = false;

        PairingCompletion onPairAndBondDone;
        PairingCompletion onSetSecurityModeDone;
        PairingCompletion onSetSecureConnectionsOnlyDone;
        PairingCompletion onSetIoCapabilitiesDone;
        PairingCompletion onGenerateOutOfBandDataDone;
        PairingCompletion onSetOutOfBandDataDone;
        infra::AutoResetFunction<void()> onRemoveAllBondsDone;
    };
}

#endif
