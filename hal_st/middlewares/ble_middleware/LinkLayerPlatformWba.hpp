#pragma once

#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/synchronous_interfaces/SynchronousRandomDataGenerator.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include "infra/util/ProxyCreator.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

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
            // The link layer needs a spare interrupt to pend and re-prioritise; its peripheral cannot use interrupts
            int32_t softwareLowInterrupt = HASH_IRQn;
        };

        // The RNG is only held while the pool the link layer draws from is refilled, so that the application can use it in between
        using RandomDataGeneratorCreator = infra::CreatorBase<SynchronousRandomDataGenerator, void()>;

        explicit LinkLayerPlatformWba(RandomDataGeneratorCreator& randomDataGeneratorCreator, const Config& config = Config());

        void ConfigureParameters();
        void Reset();
        void GenerateRandomData(infra::ByteRange result);
        int8_t MaxTransmitPower() const;

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
        void ConfigureSchedulerTimings();
        void ConfigureSleepClock() const;
        void StartSleepClockOscillator() const;
        void ConfigureRandomDataGeneratorClock() const;
        void SelectLinkLayerSleepClock() const;
        uint8_t SleepClockAccuracy() const;
        IRQn_Type SoftwareLowIrq() const;
        void RefillRandomDataPool();
        void AddToRandomDataPool(infra::MemoryRange<const uint32_t> words);
        uint32_t TakeRandomWord(std::size_t fallbackIndex);
        void ScheduleRandomDataPoolRefill();

    private:
        // ST's CFG_HW_RNG_POOL_SIZE
        static constexpr std::size_t randomDataPoolSize = 32;

        Config config;
        RandomDataGeneratorCreator& randomDataGeneratorCreator;
        std::array<uint32_t, randomDataPoolSize> randomDataPool{};
        std::size_t randomDataPoolCount = 0;
        std::atomic<bool> randomDataPoolRefillScheduled{ false };

        std::optional<cortex::ImmediateInterruptHandler> softwareLowInterrupt;

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
