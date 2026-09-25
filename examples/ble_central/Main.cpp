#include "hal_st/instantiations/NucleoUi.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/middlewares/ble_middleware/BondStorageSt.hpp"
#include "hal_st/middlewares/ble_middleware/TracingGapCentralSt.hpp"
#include "hal_st/middlewares/ble_middleware/TracingGattClientSt.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/UartStmDma.hpp"
#include "infra/stream/StringInputStream.hpp"
#include "infra/util/BoundedVector.hpp"
#include "services/ble/GapAdvertisingData.hpp"
#include "services/ble/profile/NordicUartCentral.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/TracerWithDateTime.hpp"
#include "services/util/ConfigurationStore.hpp"
#include "services/util/Terminal.hpp"
#include "services/util/TerminalWithStorage.hpp"
#include <algorithm>
#include <optional>

#if defined(STM32WB)
#include "hal_st/middlewares/ble_middleware/TracingSystemTransportLayerWb.hpp"
#include "hal_st/stm32fxxx/DefaultClockNucleoWB55RG.hpp"
#elif defined(STM32WBA)
#include "hal_st/middlewares/ble_middleware/SystemTransportLayerWba.hpp"
#include "hal_st/stm32fxxx/DefaultClockNucleoWBA55CG.hpp"
#include "hal_st/stm32fxxx/PkaStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousAesStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousRandomDataGeneratorStm.hpp"
#endif

unsigned int hse_value = 32'000'000;

namespace
{
    constexpr uint8_t numberOfLinks = 1;
    constexpr uint16_t maxAttMtuSize = 251;
    constexpr uint32_t maxNumberOfBonds = 10;

    // ST power table index, not dBm; 0x18 is the 0 dBm entry.
    constexpr uint8_t txPowerLevel = 0x18;

    const hal::MacAddress deviceAddress{ 0x0b, 0x00, 0x00, 0xe1, 0x80, 0x02 };

    const hal::GapSt::RootKeys rootKeys{
        { { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } },
        { { 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 } }
    };
}

namespace application
{
    class VolatileBondStorage
        : public services::BondStorage
    {
    public:
        void BondStorageSynchronizerCreated(services::BondStorageSynchronizer& manager) override
        {}

        void UpdateBondedDevice(hal::MacAddress address) override
        {
            if (!IsBondStored(address))
            {
                if (addresses.full())
                    addresses.erase(addresses.begin());

                addresses.push_back(address);
            }
        }

        void RemoveBond(hal::MacAddress address) override
        {
            addresses.erase(std::remove(addresses.begin(), addresses.end(), address), addresses.end());
        }

        void RemoveAllBonds() override
        {
            addresses.clear();
        }

        void RemoveBondIf(const infra::Function<bool(hal::MacAddress)>& onAddress) override
        {
            addresses.erase(std::remove_if(addresses.begin(), addresses.end(), [&onAddress](const auto& address)
                                {
                                    return onAddress(address);
                                }),
                addresses.end());
        }

        uint32_t GetMaxNumberOfBonds() const override
        {
            return addresses.max_size();
        }

        bool IsBondStored(hal::MacAddress address) const override
        {
            return std::find(addresses.begin(), addresses.end(), address) != addresses.end();
        }

        void IterateBondedDevices(const infra::Function<void(hal::MacAddress)>& onAddress) override
        {
            for (const auto& address : addresses)
                onAddress(address);
        }

    private:
        infra::BoundedVector<hal::MacAddress>::WithMaxSize<maxNumberOfBonds> addresses;
    };

    class BleCentralTerminal
        : private services::GapCentralObserver
        , private services::GapPairingObserver
        , private services::GattClientObserver
        , private services::NordicUartObserver
    {
    public:
        BleCentralTerminal(services::GapCentral& gapCentral, services::GapPairing& gapPairing, services::GapBonding& gapBonding,
            services::GattClient& gattClient, services::TerminalWithStorage& terminal, services::Tracer& tracer)
            : services::GapCentralObserver(gapCentral)
            , services::GapPairingObserver(gapPairing)
            , services::GattClientObserver(gattClient)
            , gapCentral(gapCentral)
            , gapPairing(gapPairing)
            , gapBonding(gapBonding)
            , terminal(terminal)
            , tracer(tracer)
        {
            AddCommands();
        }

    private:
        // infra::Function's default storage is two pointers, so capture this and a pointer only.
        auto Done(const char* request)
        {
            return [this, request](auto result)
            {
                Report(request, result);
            };
        }

        void AddCommands()
        {
            AddDiscoveryCommands();
            AddLinkCommands();
            AddSecurityCommands();
            AddDataCommands();
        }

        void AddDiscoveryCommands()
        {
            terminal.AddCommand({ { "scan", "sc", "start active device discovery" }, [this](const auto& params)
                {
                    Report("StartDeviceDiscovery", gapCentral.StartDeviceDiscovery(Done("StartDeviceDiscovery")));
                } });

            terminal.AddCommand({ { "stopscan", "ss", "stop device discovery" }, [this](const auto& params)
                {
                    Report("StopDeviceDiscovery", gapCentral.StopDeviceDiscovery(Done("StopDeviceDiscovery")));
                } });

            terminal.AddCommand({ { "discover", "d", "discover the Nordic UART service on the connected peer" }, [this](const auto& params)
                {
                    if (!nordicUart)
                        return static_cast<void>(tracer.Trace() << "discover refused, no connection");

                    Report("Discover", nordicUart->Discover(Done("Discover")));
                } });
        }

        void AddLinkCommands()
        {
            terminal.AddCommand({ { "connect", "c", "connect to a peer, address type 0 public or 1 random", "<aa:bb:cc:dd:ee:ff> <type>" }, [this](const auto& params)
                {
                    services::GapAddress peer{};
                    if (!ParsePeer(params, peer))
                        return Invalid("connect");

                    Report("Connect", gapCentral.Connect(peer, initiatingTimeout, Done("Connect")));
                } });

            terminal.AddCommand({ { "cancel", "ca", "cancel an outstanding connection attempt" }, [this](const auto& params)
                {
                    Report("CancelConnect", gapCentral.CancelConnect(Done("CancelConnect")));
                } });

            terminal.AddCommand({ { "disconnect", "dc", "disconnect the current link" }, [this](const auto& params)
                {
                    Report("Disconnect", gapCentral.Disconnect(Done("Disconnect")));
                } });

            terminal.AddCommand({ { "connectionparameters", "cp", "update the connection parameters, intervals in units of 1.25 ms and the timeout in units of 10 ms", "<min> <max> <latency> <timeout>" }, [this](const auto& params)
                {
                    services::GapConnectionParameters parameters{};
                    infra::StringInputStream stream{ params, infra::softFail };
                    stream >> parameters.minConnectionInterval >> " " >> parameters.maxConnectionInterval >> " " >> parameters.peripheralLatency >> " " >> parameters.supervisionTimeout;

                    if (stream.Failed() || !parameters.SupervisionTimeoutIsLongEnough())
                        return Invalid("connectionparameters");

                    Report("UpdateConnectionParameters", gapCentral.UpdateConnectionParameters(parameters, Done("UpdateConnectionParameters")));
                } });

            terminal.AddCommand({ { "phy", "ph", "request a PHY, 0 for 1M, 1 for 2M, 2 for coded", "<tx> <rx>" }, [this](const auto& params)
                {
                    uint32_t txPhy = 0;
                    uint32_t rxPhy = 0;
                    infra::StringInputStream stream{ params, infra::softFail };
                    stream >> txPhy >> " " >> rxPhy;

                    if (stream.Failed() || txPhy > 2 || rxPhy > 2)
                        return Invalid("phy");

                    Report("SetPhy", gapCentral.SetPhy(static_cast<services::GapPhy>(txPhy), static_cast<services::GapPhy>(rxPhy)));
                } });

            terminal.AddCommand({ { "datalength", "dl", "request the maximum data length for the current PHY" }, [this](const auto& params)
                {
                    Report("SetDataLength", gapCentral.SetDataLength(services::GapDataLength::Maximum(services::GapPhy::le2M)));
                } });

            terminal.AddCommand({ { "mtu", "m", "exchange the ATT MTU" }, [this](const auto& params)
                {
                    if (!connection)
                        return static_cast<void>(tracer.Trace() << "mtu refused, no connection");

                    Report("ExchangeMtu", connection->ExchangeMtu(Done("ExchangeMtu")));
                } });

            terminal.AddCommand({ { "state", "st", "show the link and pipe state" }, [this](const auto& params)
                {
                    tracer.Trace() << "state " << state << ", uart " << ((nordicUart && nordicUart->IsOpen()) ? "open" : "closed") << ", bonds " << gapBonding.GetNumberOfBonds() << "/" << gapBonding.GetMaxNumberOfBonds();
                } });
        }

        void AddSecurityCommands()
        {
            terminal.AddCommand({ { "security", "sec", "set the security mode and level, 1 to 4 for mode 1", "<level>" }, [this](const auto& params)
                {
                    uint32_t level = 0;
                    if (!Parse(params, level) || level < 1 || level > 4)
                        return Invalid("security");

                    Report("SetSecurityMode", gapPairing.SetSecurityMode(static_cast<services::GapPairing::SecurityModeAndLevel>(level - 1), Done("SetSecurityMode")));
                } });

            terminal.AddCommand({ { "iocapabilities", "io", "set the IO capabilities, 0 display, 1 displayYesNo, 2 keyboard, 3 none, 4 keyboardDisplay", "<caps>" }, [this](const auto& params)
                {
                    uint32_t capabilities = 0;
                    if (!Parse(params, capabilities) || capabilities > 4)
                        return Invalid("iocapabilities");

                    Report("SetIoCapabilities", gapPairing.SetIoCapabilities(static_cast<services::GapPairing::IoCapabilities>(capabilities), Done("SetIoCapabilities")));
                } });

            terminal.AddCommand({ { "secureconnectionsonly", "sco", "require Secure Connections for every service, 0 or 1", "<enabled>" }, [this](const auto& params)
                {
                    uint32_t enabled = 0;
                    if (!Parse(params, enabled) || enabled > 1)
                        return Invalid("secureconnectionsonly");

                    Report("SetSecureConnectionsOnly", gapPairing.SetSecureConnectionsOnly(enabled == 1, Done("SetSecureConnectionsOnly")));
                } });

            terminal.AddCommand({ { "pair", "p", "pair and bond with the connected peer" }, [this](const auto& params)
                {
                    Report("PairAndBond", gapPairing.PairAndBond(Done("PairAndBond")));
                } });

            terminal.AddCommand({ { "passkey", "pk", "answer a passkey request", "<000000-999999>" }, [this](const auto& params)
                {
                    uint32_t passkey = 0;
                    if (!Parse(params, passkey) || passkey > 999999)
                        return Invalid("passkey");

                    Report("AuthenticateWithPasskey", gapPairing.AuthenticateWithPasskey(passkey, Done("AuthenticateWithPasskey")));
                } });

            terminal.AddCommand({ { "numericcomparison", "nc", "answer a numeric comparison request, 0 or 1", "<accept>" }, [this](const auto& params)
                {
                    uint32_t accept = 0;
                    if (!Parse(params, accept) || accept > 1)
                        return Invalid("numericcomparison");

                    Report("NumericComparisonConfirm", gapPairing.NumericComparisonConfirm(accept == 1, Done("NumericComparisonConfirm")));
                } });

            terminal.AddCommand({ { "removebonds", "rb", "remove every stored bond" }, [this](const auto& params)
                {
                    Report("RemoveAllBonds", gapBonding.RemoveAllBonds([this]()
                                                 {
                                                     tracer.Trace() << "RemoveAllBonds done, " << gapBonding.GetNumberOfBonds() << " bonds left";
                                                 }));
                } });
        }

        void AddDataCommands()
        {
            terminal.AddCommand({ { "send", "s", "send text over the Nordic UART service", "<text>" }, [this](const auto& params)
                {
                    if (!nordicUart || !nordicUart->IsOpen())
                        return static_cast<void>(tracer.Trace() << "send refused, the pipe is not open");

                    if (sending)
                        return static_cast<void>(tracer.Trace() << "send refused, a send is in flight");

                    sendBuffer.assign(params.substr(0, std::min(params.size(), sendBuffer.max_size())));
                    sending = true;

                    nordicUart->SendData(infra::StringAsByteRange(sendBuffer), [this]()
                        {
                            sending = false;
                            tracer.Trace() << "send done";
                        });
                } });
        }

        bool ParsePeer(infra::BoundedConstString params, services::GapAddress& peer) const
        {
            uint32_t addressType = 0;
            infra::StringInputStream stream{ params, infra::softFail };
            stream >> infra::ToMacAddress(peer.address) >> " " >> addressType;

            if (stream.Failed() || addressType > 1)
                return false;

            peer.type = static_cast<services::GapDeviceAddressType>(addressType);
            return true;
        }

        // Implementation of services::GapCentralObserver
        void DeviceDiscovered(const services::GapAdvertisingReport& deviceDiscovered) override
        {
            services::GapAdvertisingDataParser parser{ deviceDiscovered.data };
            const auto name = parser.LocalName();

            tracer.Trace() << "discovered " << infra::AsMacAddress(deviceDiscovered.address) << " " << deviceDiscovered.addressType << " " << deviceDiscovered.eventType << " rssi " << static_cast<int32_t>(deviceDiscovered.rssi);

            if (!name.empty())
                tracer.Continue() << " name " << infra::BoundedConstString(reinterpret_cast<const char*>(name.begin()), name.size());
        }

        void StateChanged(services::GapCentralState newState) override
        {
            state = newState;
            tracer.Trace() << "state " << state;
        }

        void PhyUpdated(services::GapPhy txPhy, services::GapPhy rxPhy) override
        {
            tracer.Trace() << "phy tx " << txPhy << ", rx " << rxPhy;
        }

        void DataLengthChanged(const services::GapDataLength& dataLength) override
        {
            tracer.Trace() << "data length " << dataLength.maxTxOctets << " octets, " << dataLength.maxTxTime << " us";
        }

        // Implementation of services::GattClientObserver
        void ConnectionEstablished(infra::SharedPtr<services::GattClientConnection> establishedConnection) override
        {
            connection = establishedConnection;
            nordicUart.emplace(*connection);
            services::NordicUartObserver::Attach(*nordicUart);

            tracer.Trace() << "gatt connection established, mtu " << connection->EffectiveMaxAttMtuSize();
        }

        void ConnectionReleased(services::GattClientConnection& releasedConnection) override
        {
            nordicUart->Close();
            services::NordicUartObserver::Detach();
            nordicUart = std::nullopt;
            connection = nullptr;
            sending = false;

            tracer.Trace() << "gatt connection released";
        }

        // Implementation of services::NordicUartObserver
        void Opened() override
        {
            tracer.Trace() << "uart opened, up to " << nordicUart->MaxSendSize() << " bytes per send";
        }

        void Closed() override
        {
            sending = false;
            tracer.Trace() << "uart closed";
        }

        bool Parse(infra::BoundedConstString params, uint32_t& value) const
        {
            infra::StringInputStream stream{ params, infra::softFail };
            stream >> value;

            return !stream.Failed();
        }

        void Invalid(const char* command)
        {
            tracer.Trace() << command << ": invalid parameter";
        }

        void Report(const char* request, services::GapRequestStatus status)
        {
            if (status != services::GapRequestStatus::accepted)
                tracer.Trace() << request << " refused, status " << static_cast<uint32_t>(status);
        }

        void Report(const char* request, services::GattRequestStatus status)
        {
            if (status != services::GattRequestStatus::accepted)
                tracer.Trace() << request << " refused, status " << static_cast<uint32_t>(status);
        }

        void Report(const char* request, services::GapCentral::Result result)
        {
            tracer.Trace() << request << " done, result " << static_cast<uint32_t>(result);
        }

        void Report(const char* request, services::GapPairingResult result)
        {
            tracer.Trace() << request << " done, result " << static_cast<uint32_t>(result);
        }

        void Report(const char* request, services::GattResult result)
        {
            tracer.Trace() << request << " done, result " << static_cast<uint32_t>(result);
        }

        // Implementation of services::GapPairingObserver
        void DisplayPasskey(uint32_t passkey) override
        {
            tracer.Trace() << "enter this passkey on the peer: " << passkey;
        }

        void ConfirmNumericComparison(uint32_t value) override
        {
            tracer.Trace() << "confirm that the peer shows " << value << " with 'numericcomparison 1'";
        }

        void PairingSuccessfullyCompleted(const services::GapBondStrength& strength) override
        {
            tracer.Trace() << "paired, secure connections " << strength.secureConnections << ", authenticated " << strength.authenticated << ", key size " << strength.encryptionKeySize;
        }

        void PairingFailed(services::GapPairingResult error) override
        {
            tracer.Trace() << "pairing failed, result " << static_cast<uint32_t>(error);
        }

        void OutOfBandDataGenerated(const services::GapOutOfBandData& outOfBandData) override
        {
            tracer.Trace() << "out of band data generated for " << infra::AsMacAddress(outOfBandData.macAddress);
        }

    private:
        static constexpr infra::Duration initiatingTimeout = std::chrono::seconds(10);

        services::GapCentral& gapCentral;
        services::GapPairing& gapPairing;
        services::GapBonding& gapBonding;
        services::TerminalWithStorage& terminal;
        services::Tracer& tracer;

        services::GapCentralState state = services::GapCentralState::standby;
        infra::SharedPtr<services::GattClientConnection> connection;
        std::optional<services::NordicUartCentral> nordicUart;
        infra::BoundedString::WithStorage<64> sendBuffer;
        bool sending = false;
    };

    // Built once the controller is up, which on WB is after CPU2 reports ready.
    class BleCentral
    {
    public:
        BleCentral(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer,
            services::TerminalWithStorage& terminal, services::Tracer& tracer)
            : gapCentral(hciEventSource, bondStorageSynchronizer, gapConfiguration, tracer)
            , gattClient(hciEventSource, tracer)
            , centralTerminal(gapCentral, gapCentral, gapCentral, gattClient, terminal, tracer)
        {
            tracer.Trace() << "ble_central ready, address " << infra::AsMacAddress(deviceAddress);
        }

    private:
        hal::GapSt::GapService gapService{ "hal-st-central", 0 };
        hal::GapSt::Configuration gapConfiguration{ deviceAddress, gapService, rootKeys, hal::GapSt::justWorks, txPowerLevel, false };

        hal::TracingGapCentralSt gapCentral;
        hal::TracingGattClientSt::WithMaxConnections<numberOfLinks> gattClient;

        BleCentralTerminal centralTerminal;
    };
}

int main()
{
    HAL_Init();

#if defined(STM32WB)
    ConfigureDefaultClockNucleoWB55RG();
#elif defined(STM32WBA)
    ConfigureDefaultClockNucleoWBA55CG();
#endif

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::NUCLEO ui;
    static hal::DmaStm dmaStm;

#if defined(STM32WB)
    static hal::GpioPinStm stLinkUartTxPin{ hal::Port::B, 6 };
    static hal::GpioPinStm stLinkUartRxPin{ hal::Port::B, 7 };
    static hal::DmaStm::TransmitStream transmitStream{ dmaStm, hal::DmaChannelId{ 1, 1, DMA_REQUEST_USART1_TX } };
#elif defined(STM32WBA)
    static hal::GpioPinStm stLinkUartTxPin{ hal::Port::B, 12 };
    static hal::GpioPinStm stLinkUartRxPin{ hal::Port::A, 8 };
    static hal::DmaStm::TransmitStream transmitStream{ dmaStm, hal::DmaChannelId{ 1, 1, GPDMA1_REQUEST_USART1_TX } };
#endif

    static hal::UartStmDma stLinkUartDma{ transmitStream, 1, stLinkUartTxPin, stLinkUartRxPin };

    static services::StreamWriterOnSerialCommunication::WithStorage<256> streamWriter{ stLinkUartDma };
    static infra::TextOutputStream::WithErrorPolicy textOutputStream{ streamWriter };
    static services::TracerWithDateTime tracer{ textOutputStream };
    services::SetGlobalTracerInstance(tracer);

    static services::TerminalWithCommandsImpl::WithMaxQueueAndMaxHistory<> terminalWithCommands{ stLinkUartDma, tracer };
    static services::TerminalWithStorage::WithMaxSize<24> terminal{ terminalWithCommands, tracer };

    static std::optional<application::BleCentral> bleCentral;

#if defined(STM32WB)
    static services::ConfigurationStoreStub configurationStore;
    static std::array<uint8_t, hal::SystemTransportLayerWb::bondBlobSize> bondBlob{};
    static infra::ByteRange bondBlobRange{ infra::MakeRange(bondBlob) };

    static application::VolatileBondStorage volatileBondStorage;
    static hal::BondStorageSt bondStorageSt{ maxNumberOfBonds };
    static infra::Creator<services::BondStorageSynchronizer, services::BondStorageSynchronizerImpl, void()> bondStorageSynchronizerCreator{
        [](std::optional<services::BondStorageSynchronizerImpl>& value)
        {
            value.emplace(volatileBondStorage, bondStorageSt);
        }
    };

    static hal::TracingSystemTransportLayerWb systemTransportLayer{
        services::ConfigurationStoreAccess<infra::ByteRange>{ configurationStore, bondBlobRange },
        bondStorageSynchronizerCreator,
        { maxAttMtuSize, hal::SystemTransportLayerWb::RfWakeupClock::lowSpeedExternal, numberOfLinks },
        [](services::BondStorageSynchronizer& bondStorageSynchronizer)
        {
            bleCentral.emplace(systemTransportLayer, bondStorageSynchronizer, terminal, tracer);
        },
        tracer
    };
#elif defined(STM32WBA)
    static infra::Creator<hal::SynchronousRandomDataGenerator, hal::SynchronousRandomDataGeneratorStm, void()> randomDataGeneratorCreator;
    static infra::Creator<services::Aes128Ecb, hal::SynchronousAes128EcbStm, void()> aesCreator;
    static infra::Creator<services::EllipticCurveOperations, hal::PkaStm, void()> pkaCreator;
    hal::SystemTransportLayerWba::Config systemTransportLayerConfig;
    systemTransportLayerConfig.maxAttMtuSize = maxAttMtuSize;
    static services::ConfigurationStoreStub configurationStore;
    static std::array<uint8_t, hal::SystemTransportLayerWba::bondBlobSize> bondBlob{};
    static infra::ByteRange bondBlobRange{ infra::MakeRange(bondBlob) };
    static hal::SystemTransportLayerWba::WithLinks<numberOfLinks> systemTransportLayer{ services::ConfigurationStoreAccess<infra::ByteRange>{ configurationStore, bondBlobRange }, hal::SystemTransportLayerWba::HardwareDependencies{ randomDataGeneratorCreator, aesCreator, pkaCreator }, systemTransportLayerConfig };

    static application::VolatileBondStorage volatileBondStorage;
    static hal::BondStorageSt bondStorageSt{ maxNumberOfBonds };
    static services::BondStorageSynchronizerImpl bondStorageSynchronizer{ volatileBondStorage, bondStorageSt };

    bleCentral.emplace(systemTransportLayer, bondStorageSynchronizer, terminal, tracer);
#endif

    eventInfrastructure.Run();
    __builtin_unreachable();
}
