#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/synchronous_interfaces/SynchronousQuadratureEncoder.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/timer/Timer.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_LPTIMER)

namespace hal
{
    // Quadrature decoding in a low-power timer's encoder mode, for parts whose general-purpose
    // timers are spent elsewhere. The counter is 16 bits, clocked from the timer's internal
    // kernel clock, and wraps at the configured resolution. Unlike the timer encoder there is no
    // per-phase polarity: a mirrored wheel is handled by reversing the count instead.
    class SynchronousQuadratureEncoderLpTimStm
        : public SynchronousQuadratureEncoder
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            enum class DecodeMode : uint32_t
            {
                x2OnRisingEdges = LPTIM_CLOCKPOLARITY_RISING,
                x2OnFallingEdges = LPTIM_CLOCKPOLARITY_FALLING,
                x4OnBothEdges = LPTIM_CLOCKPOLARITY_RISING_FALLING,
            };

            enum class Filter : uint32_t
            {
                none = LPTIM_CLOCKSAMPLETIME_DIRECTTRANSITION,
                twoSamples = LPTIM_CLOCKSAMPLETIME_2TRANSITIONS,
                fourSamples = LPTIM_CLOCKSAMPLETIME_4TRANSITIONS,
                eightSamples = LPTIM_CLOCKSAMPLETIME_8TRANSITIONS,
            };

            // Counts before the counter wraps; four times the encoder's line count when
            // decoding x4. The counter runs 0..resolution-1 and resolution must fit 16 bits.
            uint32_t resolution{ 4096 };
            DecodeMode decodeMode{ DecodeMode::x4OnBothEdges };
            Filter filter{ Filter::none };

            // Reports position and direction as if the phases were swapped, so that a
            // mirrored wheel still counts up moving forward.
            bool reverse{ false };

            std::optional<std::chrono::microseconds> speedSamplePeriod;
        };

        // Input1 and input2 are the timer's IN1 and IN2 pins. Pass dummyPinStm as index when
        // the encoder has no index channel.
        SynchronousQuadratureEncoderLpTimStm(uint8_t lpTimerOneBasedIndex, GpioPinStm& input1, GpioPinStm& input2, GpioPinStm& index, const Config& config = Config());
        SynchronousQuadratureEncoderLpTimStm(const SynchronousQuadratureEncoderLpTimStm& other) = delete;
        SynchronousQuadratureEncoderLpTimStm& operator=(const SynchronousQuadratureEncoderLpTimStm& other) = delete;
        ~SynchronousQuadratureEncoderLpTimStm();

        uint32_t Position() override;
        uint32_t Resolution() override;
        MotionDirection Direction() override;
        uint32_t Speed() override;

        void EnableIndex();
        void DisableIndex();
        bool IndexAsserted() const;

    private:
        uint32_t Counter() const;
        bool CountingDown();
        void SampleSpeed();
        uint32_t CountsSince(uint32_t previous, uint32_t current, bool countingDown) const;

        uint8_t timerIndex;
        Config config;
        LPTIM_HandleTypeDef handle{};
        PeripheralPinStm input1;
        PeripheralPinStm input2;

        GpioPinStm& index;
        bool indexEnabled{ false };

        bool counterCountingDown{ false };

        std::optional<infra::TimerRepeating> speedTimer;
        uint32_t previousPosition{ 0 };
        uint32_t speed{ 0 };
    };
}

#endif
