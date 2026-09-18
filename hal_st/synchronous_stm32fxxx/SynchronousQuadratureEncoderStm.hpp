#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/synchronous_interfaces/SynchronousQuadratureEncoder.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/timer/Timer.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_TIMER)

namespace hal
{
    // Quadrature decoding in a timer's encoder mode, there being no dedicated peripheral.
    // Position is the counter and wraps at the configured resolution, there is no hardware
    // velocity capture, and the index channel is a plain input that never touches the count.
    class SynchronousQuadratureEncoderStm
        : public SynchronousQuadratureEncoder
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            enum class DecodeMode : uint32_t
            {
                x2OnPhaseA = TIM_ENCODERMODE_TI1,
                x2OnPhaseB = TIM_ENCODERMODE_TI2,
                x4OnBothPhases = TIM_ENCODERMODE_TI12,
            };

            // Counts before the counter wraps; four times the encoder's line count when
            // decoding x4. The counter runs 0..resolution-1.
            uint32_t resolution{ 4096 };
            uint32_t offset{ 0 };
            DecodeMode decodeMode{ DecodeMode::x4OnBothPhases };

            // Mirrored wheels need opposite settings to both count up moving forward.
            bool invertPhaseA{ false };
            bool invertPhaseB{ false };

            uint8_t filter{ 0 };

            // Left unset, no sampler runs and Speed() reads zero, which suits callers that
            // difference Position() on their own control loop. Motion beyond half a
            // revolution within one period is indistinguishable from motion the other way.
            std::optional<std::chrono::microseconds> speedSamplePeriod;
        };

        // Pass dummyPinStm as index when the encoder has no index channel.
        SynchronousQuadratureEncoderStm(uint8_t timerOneBasedIndex, GpioPinStm& phaseA, GpioPinStm& phaseB, GpioPinStm& index, const Config& config = Config());
        SynchronousQuadratureEncoderStm(const SynchronousQuadratureEncoderStm& other) = delete;
        SynchronousQuadratureEncoderStm& operator=(const SynchronousQuadratureEncoderStm& other) = delete;
        ~SynchronousQuadratureEncoderStm();

        uint32_t Position() override;
        uint32_t Resolution() override;
        MotionDirection Direction() override;
        uint32_t Speed() override;

        // Homing and diagnostics only; the count is never disturbed.
        void EnableIndex();
        void DisableIndex();
        bool IndexAsserted() const;

    private:
        void SampleSpeed();
        uint32_t CountsSince(uint32_t previous, uint32_t current, bool countingDown) const;

        uint8_t timerIndex;
        Config config;
        TIM_HandleTypeDef handle{};
        PeripheralPinStm phaseA;
        PeripheralPinStm phaseB;

        GpioPinStm& index;
        bool indexEnabled{ false };

        std::optional<infra::TimerRepeating> speedTimer;
        uint32_t previousPosition{ 0 };
        uint32_t speed{ 0 };
    };
}

#endif
