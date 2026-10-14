#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/Pwm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_HRTIM)

namespace hal
{
    class HighResolutionTimerStm
    {
    public:
        HighResolutionTimerStm();
        HighResolutionTimerStm(const HighResolutionTimerStm& other) = delete;
        HighResolutionTimerStm& operator=(const HighResolutionTimerStm& other) = delete;
        ~HighResolutionTimerStm();

        HRTIM_HandleTypeDef& Handle();
        static uint32_t Clock();

    private:
        HRTIM_HandleTypeDef handle{};
    };

    // The Start() overload with two duty cycles needs Outputs::inPhase.
    class HighResolutionPwmStm
        : public SingleChannelPwm
        , public TwoChannelsPwm
    {
    public:
        enum class Unit : uint8_t
        {
            a,
            b,
            c,
            d,
            e,
            f
        };

        enum class Outputs : uint8_t
        {
            inPhase,
            complementary,
            shifted
        };

        struct DeadTime
        {
            constexpr DeadTime()
            {}

            std::chrono::nanoseconds rising{ 0 };
            std::chrono::nanoseconds falling{ 0 };
        };

        // A fault keeps the outputs inactive until the next Start(), so it cannot silently re-energise a bridge.
        struct FaultInput
        {
            constexpr FaultInput()
            {}

            uint8_t input{ 1 };
            bool activeHigh{ false };
        };

        struct Config
        {
            constexpr Config()
            {}

            Outputs outputs{ Outputs::inPhase };
            std::optional<DeadTime> deadTime;
            std::optional<FaultInput> faultInput;
            std::optional<uint8_t> adcTrigger;
        };

        struct Timing
        {
            uint8_t prescalerRatio;
            uint32_t period;
        };

        static constexpr uint32_t maximumPeriod = 0xffdf;
        static constexpr uint16_t maximumDeadTimeTicks = 511;
        static constexpr uint8_t faultInputs = 6;
        static constexpr uint8_t adcTriggers = 10;

        static std::optional<Timing> TimingFor(uint32_t timerClock, Hertz frequency);
        static uint32_t MinimumTicks(uint8_t prescalerRatio);
        static bool AdcTriggerAvailable(Unit unit, uint8_t trigger);

        HighResolutionPwmStm(HighResolutionTimerStm& timer, Unit unit, GpioPinStm& output1, GpioPinStm& output2, const Config& config = Config());
        HighResolutionPwmStm(HighResolutionTimerStm& timer, Unit unit, GpioPinStm& output1, GpioPinStm& output2, GpioPinStm& faultPin, const Config& config = Config());
        HighResolutionPwmStm(const HighResolutionPwmStm& other) = delete;
        HighResolutionPwmStm& operator=(const HighResolutionPwmStm& other) = delete;
        ~HighResolutionPwmStm();

        void SetBaseFrequency(Hertz baseFrequency) override;
        void Start(DutyCycle globalDutyCycle) override;
        void Start(DutyCycle dutyCycle1, DutyCycle dutyCycle2) override;
        void Stop() override;

        void SetPhase(DutyCycle newPhase);
        bool FaultActive();

        std::optional<Timing> CurrentTiming() const;
        uint32_t Compare(uint8_t output) const;
        uint16_t DeadTimeTicks() const;
        uint8_t DeadTimePrescaler() const;

    private:
        void ConfigureTimer(const Config& config);
        void ConfigureOutputs();
        void ConfigureDeadTime(const DeadTime& deadTime);
        void ConfigureFault(const FaultInput& fault);
        void ConfigureAdcTrigger(uint8_t trigger);
        void StartImpl();
        void ApplyCompares();
        uint32_t Clamped(uint32_t ticks) const;
        uint32_t DeadTimeClock() const;
        uint32_t OutputMask() const;

    private:
        HighResolutionTimerStm& timer;
        Unit unit;
        uint32_t timerIndex;
        PeripheralPinStm output1;
        PeripheralPinStm output2;
        std::optional<PeripheralPinStm> faultPin;
        Outputs outputs;
        std::optional<uint8_t> faultInput;
        std::optional<Timing> timing;
        std::array<DutyCycle, 2> dutyCycles{};
        DutyCycle phase{};
        uint16_t deadTimeTicks = 0;
        uint8_t deadTimePrescaler = 0;
        bool running = false;
    };
}

#endif
