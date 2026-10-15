#include "validation/ble_firmware/BleGroup.hpp"
#include "infra/stream/StringInputStream.hpp"
#include "validation/firmware/Payload.hpp"
#include <chrono>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        const hal::MacAddress centralAddress{ 0x0b, 0x00, 0x00, 0xe1, 0x80, 0x02 };
        const hal::MacAddress peripheralAddress{ 0x0a, 0x00, 0x00, 0xe1, 0x80, 0x02 };
        constexpr const char* defaultName = "hal-st-periph";

        constexpr uint32_t defaultAdvertiseIntervalMs = 100;
        constexpr uint32_t scanTimeoutMaxMs = 600'000;
        constexpr uint32_t defaultConnectTimeoutMs = 5000;

        constexpr std::array<services::HilChoice<BleRole>, 2> roleChoices{ {
            { "central", BleRole::central },
            { "peripheral", BleRole::peripheral },
        } };

        constexpr std::array<services::HilChoice<services::GapDeviceAddressType>, 2> addressTypeChoices{ {
            { "public", services::GapDeviceAddressType::publicAddress },
            { "random", services::GapDeviceAddressType::randomAddress },
        } };

        constexpr std::array<services::HilChoice<services::GapPhy>, 3> phyChoices{ {
            { "1m", services::GapPhy::le1M },
            { "2m", services::GapPhy::le2M },
            { "coded", services::GapPhy::leCoded },
        } };

        HilStatus ToHil(services::GapRequestStatus status)
        {
            switch (status)
            {
                case services::GapRequestStatus::accepted:
                    return HilStatus::done;
                case services::GapRequestStatus::invalidParameter:
                    return HilStatus::range;
                case services::GapRequestStatus::busy:
                    return HilStatus::busy;
                case services::GapRequestStatus::notSupported:
                    return HilStatus::unsupported;
                default:
                    return HilStatus::failed;
            }
        }

        HilStatus ToHil(services::GattRequestStatus status)
        {
            switch (status)
            {
                case services::GattRequestStatus::accepted:
                    return HilStatus::done;
                case services::GattRequestStatus::invalidParameter:
                    return HilStatus::range;
                case services::GattRequestStatus::busy:
                    return HilStatus::busy;
                case services::GattRequestStatus::notSupported:
                    return HilStatus::unsupported;
                default:
                    return HilStatus::failed;
            }
        }

        bool ParseAddress(infra::BoundedConstString text, hal::MacAddress& address)
        {
            infra::StringInputStream stream{ text, infra::softFail };
            stream >> infra::ToMacAddress(address);

            return !stream.Failed();
        }

        // Reads an optional `key=<mac>`; false when it is there and malformed.
        bool ParseAddressKey(const services::HilArguments& arguments, const char* key, std::optional<hal::MacAddress>& address)
        {
            auto text = arguments.Key(key);

            if (!text)
                return true;

            hal::MacAddress parsed{};

            if (!ParseAddress(*text, parsed))
                return false;

            address = parsed;
            return true;
        }
    }

    BleCommands::BleCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , reporter(context.response)
        , commands{ {
              services::HilBind<BleCommands, &BleCommands::Info>("ble.info", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Open>("ble.open", "role=central|peripheral [address=<mac>]", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Advertise>("ble.advertise", "[interval=<ms>] [name=<text>]", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Standby>("ble.standby", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::AllowPairing>("ble.allowpairing", "<0|1>", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Scan>("ble.scan", "[addr=<mac>] [name=<text>] [timeout=<ms>]", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::StopScan>("ble.stopscan", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Connect>("ble.connect", "<mac> [type=public|random] [timeout=<ms>]", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Cancel>("ble.cancel", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Disconnect>("ble.disconnect", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Phy>("ble.phy", "tx=1m|2m|coded rx=1m|2m|coded", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::DataLength>("ble.datalength", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Mtu>("ble.mtu", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Discover>("ble.discover", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::ConnectionParameters>("ble.conparams", "min=<n> max=<n> latency=<n> timeout=<n>", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Link>("ble.link", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Send>("ble.send", "<hex>|- [len=<n> pattern=<p> seed=<n>]", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Security>("ble.security", "level=1|2", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Pair>("ble.pair", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::Bonds>("ble.bonds", "", *this, context.response),
              services::HilBind<BleCommands, &BleCommands::RemoveBonds>("ble.removebonds", "", *this, context.response),
          } }
    {}

    infra::MemoryRange<const BleCommands::Command> BleCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus BleCommands::Info(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto line = context.response.Ok();
        line << " ready=" << (phase == Phase::ready ? "1" : "0") << " role=";

        if (phase == Phase::closed)
            line << "none address=-";
        else
        {
            line << (requestedRole == BleRole::central ? "central" : "peripheral") << " address=";
            BleReporter::PrintAddress(line, requestedAddress);
        }

        controller.PrintStack(line);
        return HilStatus::done;
    }

    HilStatus BleCommands::Open(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "role", "address" }) || !arguments.Has("role"))
            return HilStatus::usage;

        auto status = HilStatus::done;
        auto role = BleRole::central;
        arguments.Select("role", role, roleChoices, status);

        if (status != HilStatus::done)
            return status;

        auto address = role == BleRole::central ? centralAddress : peripheralAddress;

        if (auto text = arguments.Key("address"))
            if (!ParseAddress(*text, address))
                return HilStatus::usage;

        if (phase != Phase::closed)
            return HilStatus::busy;

        requestedRole = role;
        requestedAddress = address;
        phase = Phase::starting;

        {
            auto line = context.response.Ok();
            line << " address=";
            BleReporter::PrintAddress(line, address);
        }

        controller.Start([this](hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer)
            {
                RoleReady(hciEventSource, bondStorageSynchronizer);
            });

        return HilStatus::done;
    }

    HilStatus BleCommands::Advertise(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "interval", "name" }))
            return HilStatus::usage;

        auto status = HilStatus::done;
        uint32_t interval = defaultAdvertiseIntervalMs;
        arguments.Number("interval", interval, 20, 10240, status);

        infra::BoundedConstString name(defaultName);

        if (auto text = arguments.Key("name"))
        {
            if (text->empty())
                status = HilStatus::usage;
            else if (text->size() > bleNameCapacity)
                status = HilStatus::range;
            else
                name = *text;
        }

        if (status != HilStatus::done)
            return status;

        status = NeedPeripheral();

        if (status != HilStatus::done)
            return status;

        return Accepted(peripheral->Advertise(interval, name));
    }

    HilStatus BleCommands::Standby(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedPeripheral();

        if (status != HilStatus::done)
            return status;

        if (peripheral->State() == services::GapPeripheralState::standby)
        {
            context.response.Ok();
            return HilStatus::done;
        }

        return Accepted(peripheral->Gap().Standby([](services::GapPeripheral::Result)
            {}));
    }

    HilStatus BleCommands::AllowPairing(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        auto status = HilStatus::done;
        uint32_t allow = 0;
        arguments.NumberAt(0, allow, 0, 1, status);

        if (status != HilStatus::done)
            return status;

        status = NeedPeripheral();

        if (status != HilStatus::done)
            return status;

        return Accepted(peripheral->Gap().AllowPairing(allow != 0, [](services::GapPairingResult)
            {}));
    }

    HilStatus BleCommands::Scan(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "addr", "name", "timeout" }))
            return HilStatus::usage;

        std::optional<hal::MacAddress> address;

        if (!ParseAddressKey(arguments, "addr", address))
            return HilStatus::usage;

        auto status = HilStatus::done;
        std::optional<infra::BoundedConstString> name;

        if (auto text = arguments.Key("name"))
        {
            if (text->empty())
                status = HilStatus::usage;
            else if (text->size() > bleNameCapacity)
                status = HilStatus::range;
            else
                name = *text;
        }

        std::optional<uint32_t> timeout;

        if (arguments.Has("timeout"))
        {
            uint32_t value = 0;
            arguments.Number("timeout", value, 1, scanTimeoutMaxMs, status);
            timeout = value;
        }

        if (status != HilStatus::done)
            return status;

        status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        if (central->State() != services::GapCentralState::standby)
            return HilStatus::busy;

        return Accepted(central->StartScan(address, name, timeout));
    }

    HilStatus BleCommands::StopScan(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        if (central->State() != services::GapCentralState::scanning)
        {
            context.response.Ok();
            return HilStatus::done;
        }

        return Accepted(central->StopScan());
    }

    HilStatus BleCommands::Connect(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "type", "timeout" }))
            return HilStatus::usage;

        hal::MacAddress address{};

        if (!ParseAddress(arguments.Positional(0), address))
            return HilStatus::usage;

        auto status = HilStatus::done;
        auto type = services::GapDeviceAddressType::publicAddress;
        uint32_t timeout = defaultConnectTimeoutMs;
        arguments.Select("type", type, addressTypeChoices, status);
        arguments.Number("timeout", timeout, 100, 60'000, status);

        if (status != HilStatus::done)
            return status;

        status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        if (central->State() != services::GapCentralState::standby)
            return HilStatus::busy;

        cancelRequested = false;

        status = Accepted(central->Gap().Connect(services::GapAddress{ address, type }, std::chrono::milliseconds(timeout), [this](services::GapCentral::Result result)
            {
                ConnectDone(result);
            }));

        return status;
    }

    HilStatus BleCommands::Cancel(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        if (central->State() != services::GapCentralState::initiating)
        {
            context.response.Ok();
            return HilStatus::done;
        }

        cancelRequested = true;
        status = Accepted(central->Gap().CancelConnect([](services::GapCentral::Result)
            {}));

        if (status != HilStatus::done)
            cancelRequested = false;

        return status;
    }

    HilStatus BleCommands::Disconnect(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedRole();

        if (status != HilStatus::done)
            return status;

        if (!reporter.Link().connected)
        {
            context.response.Ok();
            return HilStatus::done;
        }

        if (central)
            return Accepted(central->Gap().Disconnect([](services::GapCentral::Result)
                {}));

        return Accepted(peripheral->Gap().Standby([](services::GapPeripheral::Result)
            {}));
    }

    HilStatus BleCommands::Phy(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "tx", "rx" }) || !arguments.Has("tx") || !arguments.Has("rx"))
            return HilStatus::usage;

        auto status = HilStatus::done;
        auto tx = services::GapPhy::le1M;
        auto rx = services::GapPhy::le1M;
        arguments.Select("tx", tx, phyChoices, status);
        arguments.Select("rx", rx, phyChoices, status);

        if (status != HilStatus::done)
            return status;

        status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        return Accepted(central->Gap().SetPhy(tx, rx));
    }

    HilStatus BleCommands::DataLength(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        return Accepted(central->Gap().SetDataLength(services::GapDataLength::Maximum(services::GapPhy::le2M)));
    }

    HilStatus BleCommands::Mtu(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        return Accepted(central->ExchangeMtu());
    }

    HilStatus BleCommands::Discover(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedCentral();

        if (status != HilStatus::done)
            return status;

        return Accepted(central->Discover());
    }

    HilStatus BleCommands::ConnectionParameters(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "min", "max", "latency", "timeout" }) || !arguments.Has("min") || !arguments.Has("max") || !arguments.Has("latency") || !arguments.Has("timeout"))
            return HilStatus::usage;

        using Parameters = services::GapConnectionParameters;

        auto status = HilStatus::done;
        uint32_t minimum = 0;
        uint32_t maximum = 0;
        uint32_t latency = 0;
        uint32_t timeout = 0;
        arguments.Number("min", minimum, Parameters::connectionIntervalMultiplierMin, Parameters::connectionIntervalMultiplierMax, status);
        arguments.Number("max", maximum, Parameters::connectionIntervalMultiplierMin, Parameters::connectionIntervalMultiplierMax, status);
        arguments.Number("latency", latency, 0, 499, status);
        arguments.Number("timeout", timeout, Parameters::supervisionTimeoutMultiplierMin, Parameters::supervisionTimeoutMultiplierMax, status);

        if (status != HilStatus::done)
            return status;

        const Parameters parameters{ static_cast<uint16_t>(minimum), static_cast<uint16_t>(maximum), static_cast<uint16_t>(latency), static_cast<uint16_t>(timeout) };

        if (minimum > maximum || !parameters.SupervisionTimeoutIsLongEnough())
            return HilStatus::range;

        status = NeedRole();

        if (status != HilStatus::done)
            return status;

        if (central)
            return Accepted(central->Gap().UpdateConnectionParameters(parameters, [](services::GapCentral::Result)
                {}));

        return Accepted(peripheral->Gap().RequestConnectionParameterUpdate(parameters, [](services::GapPeripheral::Result)
            {}));
    }

    HilStatus BleCommands::Link(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedRole();

        if (status != HilStatus::done)
            return status;

        const auto& link = reporter.Link();
        auto line = context.response.Ok();
        line << " state=" << link.state << " connected=" << (link.connected ? "1" : "0");

        if (!link.connected)
            return HilStatus::done;

        line << " peer=";
        BleReporter::PrintAddress(line, link.peer);
        line << " interval=" << static_cast<uint32_t>(link.interval) << " latency=" << static_cast<uint32_t>(link.latency) << " timeout=" << static_cast<uint32_t>(link.timeout)
             << " txphy=" << BleReporter::PhyName(link.txPhy) << " rxphy=" << BleReporter::PhyName(link.rxPhy)
             << " txoctets=" << static_cast<uint32_t>(link.txOctets) << " rxoctets=" << static_cast<uint32_t>(link.rxOctets) << " mtu=" << static_cast<uint32_t>(link.mtu);
        return HilStatus::done;
    }

    HilStatus BleCommands::Send(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "len", "pattern", "seed" }))
            return HilStatus::usage;

        infra::ByteRange payload;
        auto status = ParsePayload(arguments, 0, infra::MakeRange(sendBuffer), payload);

        if (status == HilStatus::done && payload.empty())
            status = HilStatus::range;

        if (status != HilStatus::done)
            return status;

        auto* uart = Uart();

        if (uart == nullptr || !uart->IsOpen())
            return HilStatus::notOpen;

        if (payload.size() > uart->MaxSendSize())
            return HilStatus::range;

        // A send in flight when the link dropped never completes.
        if (sending && sendingOnConnection != reporter.Connections())
            sending = false;

        if (sending)
            return HilStatus::busy;

        sending = true;
        sendingOnConnection = reporter.Connections();
        uart->SendData(payload, [this]()
            {
                sending = false;
                reporter.Sent();
            });

        auto line = context.response.Ok();
        PrintData(line, payload, Output::crc);
        return HilStatus::done;
    }

    HilStatus BleCommands::Security(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "level" }) || !arguments.Has("level"))
            return HilStatus::usage;

        auto status = HilStatus::done;
        uint32_t level = 1;
        arguments.Number("level", level, 1, 2, status);

        if (status != HilStatus::done)
            return status;

        status = NeedRole();

        if (status != HilStatus::done)
            return status;

        const auto modeAndLevel = level == 1 ? services::GapPairing::SecurityModeAndLevel::mode1Level1 : services::GapPairing::SecurityModeAndLevel::mode1Level2;

        return Accepted(Pairing().SetSecurityMode(modeAndLevel, [this](services::GapPairingResult result)
            {
                reporter.Security(static_cast<uint32_t>(result));
            }));
    }

    HilStatus BleCommands::Pair(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedRole();

        if (status != HilStatus::done)
            return status;

        return Accepted(Pairing().PairAndBond([](services::GapPairingResult)
            {}));
    }

    HilStatus BleCommands::Bonds(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedRole();

        if (status != HilStatus::done)
            return status;

        context.response.Ok() << " count=" << static_cast<uint32_t>(Bonding().GetNumberOfBonds()) << " max=" << static_cast<uint32_t>(Bonding().GetMaxNumberOfBonds());
        return HilStatus::done;
    }

    HilStatus BleCommands::RemoveBonds(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto status = NeedRole();

        if (status != HilStatus::done)
            return status;

        return Accepted(Bonding().RemoveAllBonds([this]()
            {
                reporter.Bonds(Bonding().GetNumberOfBonds());
            }));
    }

    // notopen before `ble.open`, busy while the stack starts, unsupported for the other role's commands.
    HilStatus BleCommands::NeedRole() const
    {
        if (phase == Phase::closed)
            return HilStatus::notOpen;

        return phase == Phase::ready ? HilStatus::done : HilStatus::busy;
    }

    HilStatus BleCommands::NeedCentral() const
    {
        if (phase == Phase::closed)
            return HilStatus::notOpen;

        return requestedRole == BleRole::central ? NeedRole() : HilStatus::unsupported;
    }

    HilStatus BleCommands::NeedPeripheral() const
    {
        if (phase == Phase::closed)
            return HilStatus::notOpen;

        return requestedRole == BleRole::peripheral ? NeedRole() : HilStatus::unsupported;
    }

    HilStatus BleCommands::Accepted(services::GapRequestStatus status)
    {
        auto hil = ToHil(status);

        if (hil == HilStatus::done)
            context.response.Ok();

        return hil;
    }

    HilStatus BleCommands::Accepted(services::GattRequestStatus status)
    {
        auto hil = ToHil(status);

        if (hil == HilStatus::done)
            context.response.Ok();

        return hil;
    }

    void BleCommands::RoleReady(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer)
    {
        if (requestedRole == BleRole::central)
            central.emplace(hciEventSource, bondStorageSynchronizer, requestedAddress, reporter);
        else
            peripheral.emplace(hciEventSource, bondStorageSynchronizer, requestedAddress, reporter);

        phase = Phase::ready;
        reporter.Ready();
    }

    void BleCommands::ConnectDone(services::GapCentral::Result result)
    {
        using Result = services::GapCentral::Result;

        const bool cancelled = result == Result::cancelled || cancelRequested;
        cancelRequested = false;

        if (result == Result::success)
            return;

        reporter.ConnectFailed(cancelled ? "cancelled" : result == Result::timeout ? "timeout" : "failed");
    }

    services::GapPairing& BleCommands::Pairing()
    {
        if (central)
            return central->Gap();

        return peripheral->Gap();
    }

    services::GapBonding& BleCommands::Bonding()
    {
        if (central)
            return central->Gap();

        return peripheral->Gap();
    }

    services::NordicUart* BleCommands::Uart()
    {
        if (central)
            return central->Uart();

        return peripheral ? &peripheral->Uart() : nullptr;
    }
}
