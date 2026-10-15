#include "validation/ble_firmware/BleController.hpp"
#include "hal_st/middlewares/ble_middleware/BondStorageSt.hpp"
#include "infra/util/ByteRange.hpp"
#include "services/ble/BondStorageSynchronizer.hpp"
#include "services/ble/VolatileBondStorage.hpp"
#include "services/util/ConfigurationStore.hpp"
#include "validation/ble_firmware/BleRoles.hpp"
#include <array>
#include <optional>

#if defined(STM32WB)
#include "hal_st/middlewares/ble_middleware/SystemTransportLayerWb.hpp"
#elif defined(STM32WBA)
#include "hal_st/middlewares/ble_middleware/SystemTransportLayerWba.hpp"
#include "hal_st/stm32fxxx/PkaStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousAesStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousRandomDataGeneratorStm.hpp"
#endif

namespace validation
{
    namespace
    {
        constexpr uint8_t numberOfLinks = 1;

#if defined(STM32WB)
        std::optional<hal::SystemTransportLayerWb> transport;
#elif defined(STM32WBA)
        std::optional<hal::SystemTransportLayerWba::WithLinks<numberOfLinks>> transport;
#endif
    }

    // Mirrors the setup of the ble_peripheral and ble_central demos. The objects are function statics, as there, so
    // they are built when the first `Start` runs and live for the rest of the boot.
    void BleController::Start(const OnReady& onReadyCallback)
    {
        onReady = onReadyCallback;

        static services::ConfigurationStoreStub configurationStore;
        static services::VolatileBondStorage::WithMaxBonds<bleMaxBonds> volatileBondStorage;
        static hal::BondStorageSt bondStorageSt{ bleMaxBonds };

#if defined(STM32WB)
        static std::array<uint8_t, hal::SystemTransportLayerWb::bondBlobSize> bondBlob{};
        static infra::ByteRange bondBlobRange{ infra::MakeRange(bondBlob) };

        static infra::Creator<services::BondStorageSynchronizer, services::BondStorageSynchronizerImpl, void()> bondStorageSynchronizerCreator{
            [](std::optional<services::BondStorageSynchronizerImpl>& value)
            {
                value.emplace(volatileBondStorage, bondStorageSt);
            }
        };

        transport.emplace(
            services::ConfigurationStoreAccess<infra::ByteRange>{ configurationStore, bondBlobRange },
            bondStorageSynchronizerCreator,
            hal::SystemTransportLayerWb::Configuration{ bleMaxAttMtuSize, hal::SystemTransportLayerWb::RfWakeupClock::lowSpeedExternal, numberOfLinks },
            [this](services::BondStorageSynchronizer& bondStorageSynchronizer)
            {
                onReady(*transport, bondStorageSynchronizer);
            });
#elif defined(STM32WBA)
        static infra::Creator<hal::SynchronousRandomDataGenerator, hal::SynchronousRandomDataGeneratorStm, void()> randomDataGeneratorCreator;
        static infra::Creator<services::Aes128Ecb, hal::SynchronousAes128EcbStm, void()> aesCreator;
        static infra::Creator<services::EllipticCurveOperations, hal::PkaStm, void()> pkaCreator;

        static std::array<uint8_t, hal::SystemTransportLayerWba::bondBlobSize> bondBlob{};
        static infra::ByteRange bondBlobRange{ infra::MakeRange(bondBlob) };

        static services::BondStorageSynchronizerImpl bondStorageSynchronizer{ volatileBondStorage, bondStorageSt };

        hal::SystemTransportLayerWba::Config config;
        config.maxAttMtuSize = bleMaxAttMtuSize;

        transport.emplace(
            services::ConfigurationStoreAccess<infra::ByteRange>{ configurationStore, bondBlobRange },
            hal::SystemTransportLayerWba::HardwareDependencies{ randomDataGeneratorCreator, aesCreator, pkaCreator },
            config);

        onReady(*transport, bondStorageSynchronizer);
#endif
    }

    void BleController::PrintStack(services::HilResponse::Line& line) const
    {
        if (!transport)
            return static_cast<void>(line << " stack=-");

#if defined(STM32WB)
        const auto version = transport->GetVersion();
        line << " stack=" << static_cast<uint32_t>(version.firmwareMajor) << "." << static_cast<uint32_t>(version.firmwareMinor) << "." << static_cast<uint32_t>(version.firmwareSub);
#elif defined(STM32WBA)
        line << " stack=build" << static_cast<uint32_t>(transport->GetVersion().firmwareBuildNumber);
#endif
    }
}
