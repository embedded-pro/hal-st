#pragma once

#include "hal/interfaces/DigitalToAnalogPin.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace main_
{
    // The potentiometer on PA0_C is read continuously and the DAC runs a triangle wave on PA5. With a wire from PA5 to PA1_C the loopback command reads the DAC back
    class AnalogMonitor
    {
    public:
        static constexpr uint16_t dacMaximum = 4095;

        AnalogMonitor(hal::AdcStm& adc, hal::DigitalToAnalogPinImplBase& output, const infra::Function<void(uint16_t counts)>& onPotentiometer, const infra::Function<void(uint16_t counts)>& onDac);

        void SetOutput(uint16_t value);
        void StartWave();
        void MeasureInput();
        void Loopback();

    private:
        static hal::AnalogToDigitalChannelStm::Config InputConfig();

        bool Claim();
        void Release();
        void SamplePotentiometer();
        void StepWave();
        void Apply(uint16_t value);
        void LoopbackApplied();

    private:
        hal::AnalogToDigitalChannelStm potentiometer;
        hal::AnalogToDigitalChannelStm loopbackInput;
        hal::DigitalToAnalogPinImplBase& output;
        infra::Function<void(uint16_t counts)> onPotentiometer;
        infra::Function<void(uint16_t counts)> onDac;
        infra::TimerRepeating sampleTimer;
        infra::TimerRepeating waveTimer;
        infra::TimerSingleShot settleTimer;
        bool busy{ false };
        bool rising{ true };
        std::size_t level{ 0 };
        uint16_t applied{ 0 };
        uint16_t waveValue{ 0 };
    };
}
