#include "hal_st/instantiations/NucleoUi.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/middlewares/ble_middleware/BondStorageSt.hpp"
#include "hal_st/middlewares/ble_middleware/TracingGapPeripheralSt.hpp"
#include "hal_st/middlewares/ble_middleware/TracingGattServerSt.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/UartStmDma.hpp"
#include "infra/stream/StringInputStream.hpp"
#include "infra/util/BoundedVector.hpp"
#include "services/ble/GapAdvertisingData.hpp"
#include "services/ble/profile/BatteryService.hpp"
#include "services/ble/profile/DeviceInformationService.hpp"
#include "services/ble/profile/GenericAttributeService.hpp"
#include "services/ble/profile/NordicUartPeripheral.hpp"
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

    const hal::MacAddress deviceAddress{ 0x0a, 0x00, 0x00, 0xe1, 0x80, 0x02 };

    const hal::GapSt::RootKeys rootKeys{
        { { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } },
        { { 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 } }
    };

    const services::DeviceInformation deviceInformation{
        infra::MakeStringByteRange("Embedded Pro"),
        infra::MakeStringByteRange("hal-st ble_peripheral"),
        infra::MakeStringByteRange("0000-0001"),
        {},
        infra::MakeStringByteRange("1.0.0"),
        {},
        {},
        {},
        {}
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

    // GattServerSt reports characteristic writes only; the profile also needs the CCCD write and the MTU.
    class NordicUartGattServer
        : public hal::TracingGattServerSt
    {
    public:
        NordicUartGattServer(hal::HciEventSource& hciEventSource, services::Tracer& tracer)
            : hal::TracingGattServerSt(hciEventSource, tracer)
        {}

        void AttachNordicUart(services::NordicUartPeripheral& nordicUart)
        {
            this->nordicUart = &nordicUart;
        }

        void HciEvent(hci_event_pckt& event) override
        {
            hal::TracingGattServerSt::HciEvent(event);

            if (nordicUart == nullptr)
                return;

            if (event.evt == HCI_DISCONNECTION_COMPLETE_EVT_CODE)
                return nordicUart->NotificationsEnabled(false);

            if (event.evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE)
                return;

            const auto& coreEvent = *reinterpret_cast<evt_blecore_aci*>(event.data);

            if (coreEvent.ecode == ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE)
            {
                const auto& response = *reinterpret_cast<const aci_att_exchange_mtu_resp_event_rp0*>(coreEvent.data);
                nordicUart->MaxAttMtuSizeChanged(response.Server_RX_MTU);
            }
        }

    protected:
        void HandleGattAttributeModified(aci_gatt_attribute_modified_event_rp0& event) override
        {
            hal::TracingGattServerSt::HandleGattAttributeModified(event);

            if (nordicUart == nullptr || event.Attr_Data_Length < sizeof(uint16_t))
                return;

            constexpr uint16_t clientCharacteristicConfigurationOffset = 2;

            for (const auto& characteristic : nordicUart->Service().Characteristics())
                if (characteristic.Type() == services::AttAttribute::Uuid{ services::uuid::nordicUartTx } &&
                    event.Attr_Handle == characteristic.Handle() + clientCharacteristicConfigurationOffset)
                {
                    const auto value = static_cast<uint16_t>(event.Attr_Data[0] | (event.Attr_Data[1] << 8));
                    nordicUart->NotificationsEnabled(value == infra::enum_cast(services::GattDescriptor::ClientCharacteristicConfiguration::CharacteristicValue::enableNotification));
                }
        }

    private:
        services::NordicUartPeripheral* nordicUart = nullptr;
    };

    class BlePeripheralTerminal
        : private services::GapPeripheralObserver
        , private services::GapPairingObserver
        , private services::NordicUartObserver
    {
    public:
        BlePeripheralTerminal(services::GapPeripheral& gapPeripheral, services::GapPairing& gapPairing, services::GapBonding& gapBonding,
            services::NordicUart& nordicUart, services::BatteryService& batteryService, services::TerminalWithStorage& terminal, services::Tracer& tracer)
            : services::GapPeripheralObserver(gapPeripheral)
            , services::GapPairingObserver(gapPairing)
            , services::NordicUartObserver(nordicUart)
            , gapPeripheral(gapPeripheral)
            , gapPairing(gapPairing)
            , gapBonding(gapBonding)
            , nordicUart(nordicUart)
            , batteryService(batteryService)
            , terminal(terminal)
            , tracer(tracer)
        {
            AddCommands();
            UpdateAdvertisementData();
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
            terminal.AddCommand({ { "advertise", "adv", "start connectable undirected advertising" }, [this](const auto& params)
                {
                    StartAdvertising();
                } });

            terminal.AddCommand({ { "standby", "sb", "stop advertising" }, [this](const auto& params)
                {
                    Report("Standby", gapPeripheral.Standby(Done("Standby")));
                } });

            terminal.AddCommand({ { "name", "n", "set the advertised device name", "<name>" }, [this](const auto& params)
                {
                    deviceName.assign(params.substr(0, std::min(params.size(), deviceName.max_size())));
                    UpdateAdvertisementData();
                    RestartAdvertisingIfActive();
                } });

            terminal.AddCommand({ { "address", "addr", "show the public and identity address" }, [this](const auto& params)
                {
                    tracer.Trace() << "address " << infra::AsMacAddress(gapPeripheral.GetAddress().address) << ", identity " << infra::AsMacAddress(gapPeripheral.GetIdentityAddress().address);
                } });

            terminal.AddCommand({ { "state", "st", "show the link and pipe state" }, [this](const auto& params)
                {
                    tracer.Trace() << "state " << state << ", uart " << (nordicUart.IsOpen() ? "open" : "closed") << ", bonds " << gapBonding.GetNumberOfBonds() << "/" << gapBonding.GetMaxNumberOfBonds();
                } });

            AddSecurityCommands();
            AddDataCommands();
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

            terminal.AddCommand({ { "allowpairing", "ap", "accept or refuse pairing requests, 0 or 1", "<allow>" }, [this](const auto& params)
                {
                    uint32_t allow = 0;
                    if (!Parse(params, allow) || allow > 1)
                        return Invalid("allowpairing");

                    Report("AllowPairing", gapPairing.AllowPairing(allow == 1, Done("AllowPairing")));
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
                    if (!nordicUart.IsOpen())
                        return static_cast<void>(tracer.Trace() << "send refused, no peer is subscribed");

                    if (sending)
                        return static_cast<void>(tracer.Trace() << "send refused, a send is in flight");

                    sendBuffer.assign(params.substr(0, std::min(params.size(), sendBuffer.max_size())));
                    sending = true;

                    nordicUart.SendData(infra::StringAsByteRange(sendBuffer), [this]()
                        {
                            sending = false;
                            tracer.Trace() << "send done";
                        });
                } });

            terminal.AddCommand({ { "battery", "bat", "set the battery level in percent", "<0-100>" }, [this](const auto& params)
                {
                    uint32_t level = 0;
                    if (!Parse(params, level) || level > services::BatteryService::maxBatteryLevel)
                        return Invalid("battery");

                    batteryService.BatteryLevelChanged(static_cast<uint8_t>(level));
                    tracer.Trace() << "battery level " << level << "%";
                } });
        }

        void StartAdvertising()
        {
            Report("Advertise", gapPeripheral.Advertise(services::GapAdvertisementType::advInd, advertisementIntervalMultiplier, Done("Advertise")));
        }

        // SetAdvertisementData only caches; Advertise programs the controller. Standby would drop a live link.
        void RestartAdvertisingIfActive()
        {
            if (state != services::GapPeripheralState::advertising)
                return;

            Report("Standby", gapPeripheral.Standby([this](auto result)
                                  {
                                      Report("Standby", result);
                                      StartAdvertising();
                                  }));
        }

        void UpdateAdvertisementData()
        {
            advertisementData.clear();

            services::GapAdvertisementFormatter formatter{ advertisementData };
            formatter.AppendFlags(services::GapAdvertisementFlags::leGeneralDiscoverableMode | services::GapAdvertisementFlags::brEdrNotSupported);
            formatter.AppendCompleteLocalName(deviceName);

            Report("SetAdvertisementData", gapPeripheral.SetAdvertisementData(formatter.FormattedAdvertisementData(), Done("SetAdvertisementData")));
        }

        // Implementation of services::GapPeripheralObserver
        void StateChanged(services::GapPeripheralState newState) override
        {
            state = newState;
            tracer.Trace() << "state " << state;
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

        // Implementation of services::NordicUartObserver
        void Opened() override
        {
            tracer.Trace() << "uart opened, up to " << nordicUart.MaxSendSize() << " bytes per send";
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

        void Report(const char* request, services::GapPeripheral::Result result)
        {
            tracer.Trace() << request << " done, result " << static_cast<uint32_t>(result);
        }

        void Report(const char* request, services::GapPairingResult result)
        {
            tracer.Trace() << request << " done, result " << static_cast<uint32_t>(result);
        }

    private:
        static constexpr services::GapPeripheral::AdvertisementIntervalMultiplier advertisementIntervalMultiplier = 0x00a0;

        services::GapPeripheral& gapPeripheral;
        services::GapPairing& gapPairing;
        services::GapBonding& gapBonding;
        services::NordicUart& nordicUart;
        services::BatteryService& batteryService;
        services::TerminalWithStorage& terminal;
        services::Tracer& tracer;

        services::GapPeripheralState state = services::GapPeripheralState::standby;
        infra::BoundedString::WithStorage<16> deviceName{ "hal-st-periph" };
        infra::BoundedVector<uint8_t>::WithMaxSize<services::gapMaxAdvertisementDataSize> advertisementData;
        infra::BoundedString::WithStorage<64> sendBuffer;
        bool sending = false;
    };

    // Built once the controller is up, which on WB is after CPU2 reports ready.
    class BlePeripheral
    {
    public:
        BlePeripheral(hal::HciEventSource& hciEventSource, services::BondStorageSynchronizer& bondStorageSynchronizer,
            services::TerminalWithStorage& terminal, services::Tracer& tracer)
            : gapPeripheral(hciEventSource, bondStorageSynchronizer, gapConfiguration, tracer)
            , gattServer(hciEventSource, tracer)
            , confirmIndication(hciEventSource)
            , genericAttributeService(gattServer)
            , deviceInformationService(gattServer, deviceInformation)
            , batteryService(gattServer)
            , nordicUart(gattServer, maxAttMtuSize)
            , peripheralTerminal(gapPeripheral, gapPeripheral, gapPeripheral, nordicUart, batteryService, terminal, tracer)
        {
            gattServer.AttachNordicUart(nordicUart);
            tracer.Trace() << "ble_peripheral ready, address " << infra::AsMacAddress(deviceAddress);
        }

    private:
        hal::GapSt::GapService gapService{ "hal-st-periph", 0 };
        hal::GapSt::Configuration gapConfiguration{ deviceAddress, gapService, rootKeys, hal::GapSt::justWorks, txPowerLevel, false };

        hal::TracingGapPeripheralSt gapPeripheral;
        NordicUartGattServer gattServer;
        hal::GattConfirmIndication confirmIndication;

        services::GenericAttributeService genericAttributeService;
        services::DeviceInformationService deviceInformationService;
        services::BatteryService batteryService;
        services::NordicUartPeripheral nordicUart;

        BlePeripheralTerminal peripheralTerminal;
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

    static std::optional<application::BlePeripheral> blePeripheral;

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
            blePeripheral.emplace(systemTransportLayer, bondStorageSynchronizer, terminal, tracer);
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

    blePeripheral.emplace(systemTransportLayer, bondStorageSynchronizer, terminal, tracer);
#endif

    eventInfrastructure.Run();
    __builtin_unreachable();
}
