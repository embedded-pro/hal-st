#include "validation/ble_firmware/BleReporter.hpp"
#include "infra/util/Endian.hpp"
#include "services/ble/GapAdvertisingData.hpp"
#include "validation/firmware/Payload.hpp"
#include <algorithm>
#include <array>

namespace validation
{
    namespace
    {
        constexpr std::size_t addressTextSize = 17;
        constexpr std::size_t nameTextSize = 32;

        const char* AddressTypeName(services::GapDeviceAddressType type)
        {
            return type == services::GapDeviceAddressType::publicAddress ? "public" : "random";
        }

        const char* EventTypeName(services::GapAdvertisingEventType type)
        {
            switch (type)
            {
                case services::GapAdvertisingEventType::advInd:
                    return "advInd";
                case services::GapAdvertisingEventType::advDirectInd:
                    return "advDirectInd";
                case services::GapAdvertisingEventType::advScanInd:
                    return "advScanInd";
                case services::GapAdvertisingEventType::advNonconnInd:
                    return "advNonconnInd";
                default:
                    return "scanResponse";
            }
        }

        std::size_t NameText(infra::ConstByteRange name, std::array<char, nameTextSize>& text)
        {
            auto size = std::min(name.size(), text.size());

            for (std::size_t i = 0; i != size; ++i)
                text[i] = name[i] > ' ' && name[i] <= '~' ? static_cast<char>(name[i]) : '_';

            return size;
        }
    }

    BleReporter::BleReporter(services::HilResponse& response)
        : response(response)
    {}

    const BleLinkState& BleReporter::Link() const
    {
        return link;
    }

    uint32_t BleReporter::Connections() const
    {
        return connections;
    }

    void BleReporter::Ready()
    {
        response.Event("ble") << " ready";
    }

    void BleReporter::State(const char* state)
    {
        link.state = state;
        response.Event("ble") << " state value=" << state;
    }

    void BleReporter::Found(const services::GapAdvertisingReport& report)
    {
        std::array<char, nameTextSize> text{};
        auto size = NameText(services::GapAdvertisingDataParser(report.data).LocalName(), text);

        auto line = response.Event("ble");
        line << " found addr=";
        PrintAddress(line, report.address);
        line << " type=" << AddressTypeName(report.addressType) << " event=" << EventTypeName(report.eventType);

        if (report.rssi == services::gapRssiNotAvailable || report.rssi >= 0)
            line << " rssi=0";
        else
            line << " rssi=-" << static_cast<uint32_t>(-static_cast<int32_t>(report.rssi));

        line << " name=";

        if (size == 0)
            line << "-";
        else
            line << infra::BoundedConstString(text.data(), size);
    }

    void BleReporter::Connected(const hal::MacAddress& peer, uint16_t interval, uint16_t latency, uint16_t timeout)
    {
        link = BleLinkState{ link.state, true, peer, interval, latency, timeout };
        ++connections;

        auto line = response.Event("ble");
        line << " connected peer=";
        PrintAddress(line, peer);
        line << " interval=" << static_cast<uint32_t>(interval) << " latency=" << static_cast<uint32_t>(latency) << " timeout=" << static_cast<uint32_t>(timeout);
    }

    void BleReporter::ConnectFailed(const char* reason)
    {
        response.Event("ble") << " connectfailed reason=" << reason;
    }

    void BleReporter::Disconnected(uint8_t reason)
    {
        link = BleLinkState{ link.state };
        response.Event("ble") << " disconnected reason=" << static_cast<uint32_t>(reason);
    }

    void BleReporter::ConnectionParameters(uint16_t interval, uint16_t latency, uint16_t timeout)
    {
        link.interval = interval;
        link.latency = latency;
        link.timeout = timeout;
        response.Event("ble") << " conparams interval=" << static_cast<uint32_t>(interval) << " latency=" << static_cast<uint32_t>(latency) << " timeout=" << static_cast<uint32_t>(timeout);
    }

    void BleReporter::Phy(uint8_t tx, uint8_t rx)
    {
        link.txPhy = tx;
        link.rxPhy = rx;
        response.Event("ble") << " phy tx=" << PhyName(tx) << " rx=" << PhyName(rx);
    }

    void BleReporter::DataLength(uint16_t txOctets, uint16_t txTime, uint16_t rxOctets, uint16_t rxTime)
    {
        link.txOctets = txOctets;
        link.rxOctets = rxOctets;
        response.Event("ble") << " datalength txoctets=" << static_cast<uint32_t>(txOctets) << " rxoctets=" << static_cast<uint32_t>(rxOctets)
                              << " txtime=" << static_cast<uint32_t>(txTime) << " rxtime=" << static_cast<uint32_t>(rxTime);
    }

    void BleReporter::DataLength(uint16_t txOctets, uint16_t txTime)
    {
        link.txOctets = txOctets;
        link.rxOctets = std::nullopt;
        response.Event("ble") << " datalength txoctets=" << static_cast<uint32_t>(txOctets) << " txtime=" << static_cast<uint32_t>(txTime);
    }

    void BleReporter::Mtu(uint16_t size)
    {
        link.mtu = size;
        response.Event("ble") << " mtu size=" << static_cast<uint32_t>(size);
    }

    void BleReporter::Uart(bool open, std::size_t maxSend)
    {
        auto line = response.Event("ble");
        line << " uart state=" << (open ? "open" : "closed");

        if (open)
            line << " maxsend=" << static_cast<uint32_t>(maxSend);
    }

    void BleReporter::UartFailed(uint32_t result)
    {
        response.Event("ble") << " uart state=failed result=" << result;
    }

    void BleReporter::Sent()
    {
        response.Event("ble") << " sent";
    }

    void BleReporter::Received(infra::ConstByteRange data)
    {
        infra::BigEndian<uint32_t> crc{ Crc32(data) };

        auto line = response.Event("ble");
        line << " rx len=" << static_cast<uint32_t>(data.size()) << " crc=";
        line.Hex(infra::MakeByteRange(crc));
    }

    void BleReporter::Security(uint32_t result)
    {
        response.Event("ble") << " security result=" << result;
    }

    void BleReporter::Paired(const services::GapBondStrength& strength)
    {
        response.Event("ble") << " paired secure=" << (strength.secureConnections ? "1" : "0") << " auth=" << (strength.authenticated ? "1" : "0")
                              << " keysize=" << static_cast<uint32_t>(strength.encryptionKeySize);
    }

    void BleReporter::PairingFailed(services::GapPairingResult result)
    {
        response.Event("ble") << " pairfailed result=" << static_cast<uint32_t>(result);
    }

    void BleReporter::Bonds(std::size_t count)
    {
        response.Event("ble") << " bonds count=" << static_cast<uint32_t>(count);
    }

    void BleReporter::AdvertiseFailed(uint32_t status)
    {
        response.Event("ble") << " advertisefailed status=" << status;
    }

    void BleReporter::PrintAddress(services::HilResponse::Line& line, const hal::MacAddress& address)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::array<char, addressTextSize> text{};

        for (std::size_t i = 0; i != address.size(); ++i)
        {
            text[i * 3] = digits[address[i] >> 4];
            text[i * 3 + 1] = digits[address[i] & 0x0f];

            if (i + 1 != address.size())
                text[i * 3 + 2] = ':';
        }

        line << infra::BoundedConstString(text.data(), text.size());
    }

    const char* BleReporter::PhyName(uint8_t hciPhy)
    {
        switch (hciPhy)
        {
            case 1:
                return "1m";
            case 2:
                return "2m";
            case 3:
                return "coded";
            default:
                return "unknown";
        }
    }
}
