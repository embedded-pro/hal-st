#pragma once

#include "hal_st/synchronous_stm32fxxx/SynchronousRandomDataGeneratorStm.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include <atomic>
#include <cstdint>
#include <optional>

namespace hal
{
    class LinkLayerPlatformWba
        : public infra::InterfaceConnector<LinkLayerPlatformWba>
    {
    public:
        enum class SleepClockSource : uint8_t
        {
            lse,
            lsi,
            hseDiv1000
        };

        enum class TxPowerTable : uint8_t
        {
            upTo10dBm = 0,
            upTo3dBm = 1
        };

        struct Config
        {
            constexpr Config()
            {}

            SleepClockSource sleepClockSource = SleepClockSource::lse;
            // Bluetooth Core Specification, Volume 6, Part B, section 2.3.3.1: 0 is 251 to 500 ppm, which holds for any crystal
            uint8_t lseSleepClockAccuracy = 0;
            TxPowerTable txPowerTable = TxPowerTable::upTo3dBm;
        };

        explicit LinkLayerPlatformWba(const Config& config = Config());

        void ConfigureParameters();
        void Reset();
        void GenerateRandomData(infra::ByteRange result);

        void SetupRadioInterrupt(void (*callback)());
        void SetupSoftwareLowInterrupt(void (*callback)());
        void TriggerSoftwareLowInterrupt(uint8_t priority);
        void EnableInterrupts();
        void DisableInterrupts();
        void EnableSpecificInterrupts(uint8_t isrType);
        void DisableSpecificInterrupts(uint8_t isrType);

        void NotifyWfiEnter();
        void NotifyWfiExit();
        void WaitRadioBusClockReady();

        void RadioInterrupt();
        void SoftwareLowInterrupt();

    private:
        void ConfigureSleepClock() const;
        void ConfigureRandomDataGeneratorClock() const;
        void SelectLinkLayerSleepClock() const;
        uint8_t SleepClockAccuracy() const;

    private:
        Config config;
        std::optional<SynchronousRandomDataGeneratorStm> randomDataGenerator;

        void (*radioCallback)() = nullptr;
        void (*softwareLowCallback)() = nullptr;
        std::atomic<bool> softwareLowPendingAtRadioLowPriority{ false };

        uint32_t savedPrimask = 0;
        int32_t interruptsDisabledCount = 0;
        int32_t radioInterruptDisabledCount = 0;
        int32_t softwareLowInterruptDisabledCount = 0;
        int32_t systemLowInterruptsDisabledCount = 0;
        uint32_t savedBasepri = 0;

        bool radioBusClockSwitchedOff = false;
        uint32_t sleepTimerAtWfiExit = 0;
    };
}
