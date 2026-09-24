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

            uint32_t resolution{ 4096 };
            DecodeMode decodeMode{ DecodeMode::x4OnBothEdges };
            Filter filter{ Filter::none };

            bool reverseForMirroredMounting{ false };

            std::optional<std::chrono::microseconds> speedSamplePeriod;
        };

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
        uint32_t StableCounter() const;
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

        bool lastKnownCountingDown{ false };

        std::optional<infra::TimerRepeating> speedTimer;
        uint32_t previousPosition{ 0 };
        uint32_t speed{ 0 };
    };
}

#endif
