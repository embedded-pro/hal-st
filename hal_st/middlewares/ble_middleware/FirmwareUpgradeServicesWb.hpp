#pragma once

#include <cstdint>

namespace hal
{
    class FirmwareUpgradeServices
    {
    public:
        enum class State : uint8_t
        {
            idle,
            wirelessStackUpgradeOngoing,
            firmwareUpgradeServicesUpgradeOngoing,
            serviceOngoing,
            error
        };

        struct Status
        {
            State state;
            uint8_t errorCode;
        };

        FirmwareUpgradeServices() = default;
        FirmwareUpgradeServices(const FirmwareUpgradeServices& other) = delete;
        FirmwareUpgradeServices& operator=(const FirmwareUpgradeServices& other) = delete;
        virtual ~FirmwareUpgradeServices() = default;

        virtual uint32_t SecureFlashStartAddress() const = 0;

        // Only valid while the wireless stack runs; the device resets with FUS running on CPU2
        virtual void RequestFirmwareUpgradeServices() = 0;

        // The commands below are only valid while FUS runs
        virtual Status GetStatus() = 0;
        virtual bool Upgrade(uint32_t imageAddress) = 0;
        virtual bool DeleteWirelessStack() = 0;
        virtual bool StartWirelessStack() = 0;
    };

    class FirmwareUpgradeServicesWb
        : public FirmwareUpgradeServices
    {
    public:
        uint32_t SecureFlashStartAddress() const override;
        void RequestFirmwareUpgradeServices() override;
        Status GetStatus() override;
        bool Upgrade(uint32_t imageAddress) override;
        bool DeleteWirelessStack() override;
        bool StartWirelessStack() override;
    };
}
