#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/Pwm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/MemoryRange.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_TIMER)

namespace hal
{
    class PwmStmBase
    {
    public:
        enum class Alignment : uint32_t
        {
            edgeAligned = TIM_COUNTERMODE_UP,
            edgeAlignedDownCounting = TIM_COUNTERMODE_DOWN,
            centerAlignedDownCounting = TIM_COUNTERMODE_CENTERALIGNED1,
            centerAlignedUpCounting = TIM_COUNTERMODE_CENTERALIGNED2,
            centerAlignedBothCounting = TIM_COUNTERMODE_CENTERALIGNED3,
        };

        // The compareChannelN sources fire at that channel's compare match, placing an ADC
        // conversion at a chosen point in the period. AdcTimerTriggeredBase consumes TRGO.
        enum class TriggerOutput : uint32_t
        {
            reset = TIM_TRGO_RESET,
            enable = TIM_TRGO_ENABLE,
            update = TIM_TRGO_UPDATE,
            comparePulse = TIM_TRGO_OC1,
            compareChannel1 = TIM_TRGO_OC1REF,
            compareChannel2 = TIM_TRGO_OC2REF,
            compareChannel3 = TIM_TRGO_OC3REF,
            compareChannel4 = TIM_TRGO_OC4REF,
        };

        struct DeadTime
        {
            constexpr DeadTime()
            {}

            // STM32 applies one dead-time generator to both edges, so a single duration is
            // all BDTR.DTG can express. Durations beyond its range saturate at the longest
            // it can encode, which is shorter than requested.
            std::chrono::nanoseconds duration{ 0 };
        };

        struct BreakInput
        {
            constexpr BreakInput()
            {}

            bool activeHigh{ false };

            // When clear, recovery needs an explicit Start(), so a fault cannot silently
            // re-energise the bridges.
            bool automaticOutputEnable{ false };

            uint8_t filter{ 0 };
        };

        struct Config
        {
            constexpr Config()
            {}

            Alignment alignment{ Alignment::edgeAligned };
            uint16_t prescaler{ 0 };

            // Buffers period and compare, so a duty cycle written mid-period takes effect at
            // the next update event rather than producing a runt pulse.
            bool preloadEnabled{ true };

            std::optional<TriggerOutput> triggerOutput;
            std::optional<DeadTime> deadTime;
            std::optional<BreakInput> breakInput;
        };

        struct ChannelConfig
        {
            uint8_t channel{ 1 };

            GpioPinStm& pin;
            GpioPinStm& complementaryPin{ dummyPinStm };

            bool inverted{ false };
            bool complementaryInverted{ false };

            // Reached when the outputs are disabled, which requires a break-capable instance.
            bool idleStateHigh{ false };
            bool complementaryIdleStateHigh{ false };
        };

        static constexpr std::size_t maxChannels = 4;

        // Rounds up: a dead-time shorter than requested shoots through the bridge.
        static uint8_t EncodeDeadTime(std::chrono::nanoseconds deadTime, uint32_t timerClockFrequency, uint32_t clockDivision = TIM_CLOCKDIVISION_DIV1);
        static std::chrono::nanoseconds DecodeDeadTime(uint8_t deadTimeGenerator, uint32_t timerClockFrequency, uint32_t clockDivision = TIM_CLOCKDIVISION_DIV1);

    protected:
        PwmStmBase(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, GpioPinStm& breakPin, const Config& config);
        ~PwmStmBase();

        void SetBaseFrequencyImpl(Hertz baseFrequency);
        void StartImpl(infra::MemoryRange<const Percent> dutyCycles);
        void StopImpl();

    private:
        struct Channel
        {
            Channel(uint8_t timerOneBasedIndex, const ChannelConfig& config);

            uint8_t index;
            bool complementary;
            PeripheralPinStm pin;
            std::optional<PeripheralPinStm> complementaryPin;

            Percent dutyCycle{ 0 };
        };

        void ConfigureTimeBase();
        void ConfigureTriggerOutput();
        void ConfigureChannel(const Channel& channel, const ChannelConfig& config);
        void ConfigureBreakAndDeadTime();
        void ConfigureBreakInputSource();
        void SetDutyCycle(Channel& channel, Percent dutyCycle);
        uint32_t TimerClockFrequency() const;
        uint32_t MaximumCompare() const;

        uint8_t timerIndex;
        Config config;
        bool idleStateRequested{ false };
        bool started{ false };
        TIM_HandleTypeDef handle{};
        infra::BoundedVector<Channel>::WithMaxSize<maxChannels> channels;
        std::optional<PeripheralPinStm> breakPin;
    };

    // The Start() overload used must match the number of channels constructed.
    class PwmStm
        : public PwmStmBase
        , public SingleChannelPwm
        , public TwoChannelsPwm
        , public ThreeChannelsPwm
        , public FourChannelsPwm
    {
    public:
        PwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, const Config& config = Config());
        PwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, GpioPinStm& breakPin, const Config& config = Config());

        void SetBaseFrequency(Hertz baseFrequency) override;
        void Start(Percent globalDutyCycle) override;
        void Start(Percent dutyCycle1, Percent dutyCycle2) override;
        void Start(Percent dutyCycle1, Percent dutyCycle2, Percent dutyCycle3) override;
        void Start(Percent dutyCycle1, Percent dutyCycle2, Percent dutyCycle3, Percent dutyCycle4) override;
        void Stop() override;
    };
}

#endif
