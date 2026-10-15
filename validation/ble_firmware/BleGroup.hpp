#pragma once

#include "services/ble/GapBonding.hpp"
#include "services/ble/GapPairing.hpp"
#include "services/hil/HilCommand.hpp"
#include "validation/ble_firmware/BleController.hpp"
#include "validation/ble_firmware/BleReporter.hpp"
#include "validation/ble_firmware/BleRoles.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    enum class BleRole : uint8_t
    {
        central,
        peripheral,
    };

    class BleCommands
        : public services::TerminalCommands
    {
    public:
        explicit BleCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        enum class Phase : uint8_t
        {
            closed,
            starting,
            ready,
        };

        services::HilStatus Info(const services::HilArguments& arguments);
        services::HilStatus Open(const services::HilArguments& arguments);
        services::HilStatus Advertise(const services::HilArguments& arguments);
        services::HilStatus Standby(const services::HilArguments& arguments);
        services::HilStatus AllowPairing(const services::HilArguments& arguments);
        services::HilStatus Scan(const services::HilArguments& arguments);
        services::HilStatus StopScan(const services::HilArguments& arguments);
        services::HilStatus Connect(const services::HilArguments& arguments);
        services::HilStatus Cancel(const services::HilArguments& arguments);
        services::HilStatus Disconnect(const services::HilArguments& arguments);
        services::HilStatus Phy(const services::HilArguments& arguments);
        services::HilStatus DataLength(const services::HilArguments& arguments);
        services::HilStatus Mtu(const services::HilArguments& arguments);
        services::HilStatus Discover(const services::HilArguments& arguments);
        services::HilStatus ConnectionParameters(const services::HilArguments& arguments);
        services::HilStatus Link(const services::HilArguments& arguments);
        services::HilStatus Send(const services::HilArguments& arguments);
        services::HilStatus Security(const services::HilArguments& arguments);
        services::HilStatus Pair(const services::HilArguments& arguments);
        services::HilStatus Bonds(const services::HilArguments& arguments);
        services::HilStatus RemoveBonds(const services::HilArguments& arguments);

        services::HilStatus NeedRole() const;
        services::HilStatus NeedCentral() const;
        services::HilStatus NeedPeripheral() const;
        services::HilStatus Accepted(services::GapRequestStatus status);
        services::HilStatus Accepted(services::GattRequestStatus status);

        void RoleReady(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer);
        void ConnectDone(services::GapCentral::Result result);
        services::GapPairing& Pairing();
        services::GapBonding& Bonding();
        services::NordicUart* Uart();

    private:
        services::HilContext& context;
        BleReporter reporter;
        BleController controller;

        Phase phase = Phase::closed;
        BleRole requestedRole = BleRole::central;
        hal::MacAddress requestedAddress{};
        std::optional<PeripheralRole> peripheral;
        std::optional<CentralRole> central;

        bool cancelRequested = false;
        bool sending = false;
        uint32_t sendingOnConnection = 0;
        std::array<uint8_t, bleMaxAttMtuSize - 3> sendBuffer{};

        std::array<Command, 21> commands;
    };
}
