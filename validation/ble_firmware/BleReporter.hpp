#pragma once

#include "hal/interfaces/MacAddress.hpp"
#include "infra/util/ByteRange.hpp"
#include "services/ble/GapPairing.hpp"
#include "services/ble/GapTypes.hpp"
#include "services/hil/HilResponse.hpp"
#include <cstddef>
#include <cstdint>

namespace validation
{
    struct BleLinkState
    {
        const char* state = "standby";
        bool connected = false;
        hal::MacAddress peer{};
        uint16_t interval = 0;
        uint16_t latency = 0;
        uint16_t timeout = 0;
        uint8_t txPhy = 1;
        uint8_t rxPhy = 1;
        uint16_t txOctets = 27;
        uint16_t rxOctets = 27;
        uint16_t mtu = 23;
    };

    class BleReporter
    {
    public:
        explicit BleReporter(services::HilResponse& response);

        const BleLinkState& Link() const;

        uint32_t Connections() const;

        void Ready();
        void State(const char* state);
        void Found(const services::GapAdvertisingReport& report);
        void Connected(const hal::MacAddress& peer, uint16_t interval, uint16_t latency, uint16_t timeout);
        void ConnectFailed(const char* reason);
        void Disconnected(uint8_t reason);
        void ConnectionParameters(uint16_t interval, uint16_t latency, uint16_t timeout);
        void Phy(uint8_t tx, uint8_t rx);
        void DataLength(uint16_t txOctets, uint16_t txTime, uint16_t rxOctets, uint16_t rxTime);
        void Mtu(uint16_t size);
        void Uart(bool open, std::size_t maxSend);
        void UartFailed(uint32_t result);
        void Sent();
        void Received(infra::ConstByteRange data);
        void Security(uint32_t result);
        void Paired(const services::GapBondStrength& strength);
        void PairingFailed(services::GapPairingResult result);
        void Bonds(std::size_t count);
        void AdvertiseFailed(uint32_t status);

        static void PrintAddress(services::HilResponse::Line& line, const hal::MacAddress& address);
        static const char* PhyName(uint8_t hciPhy);

    private:
        services::HilResponse& response;
        BleLinkState link;
        uint32_t connections = 0;
    };
}
