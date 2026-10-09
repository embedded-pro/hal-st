#include "demo/stm32h757i_eval/AnalogMonitor.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <chrono>

namespace main_
{
    namespace
    {
        // The DAC output buffer cannot reach the rails, so the sweep stays clear of 0 and 4095
        constexpr std::array<uint16_t, 5> loopbackLevels{ 512, 1024, 2048, 3072, 3584 };
        constexpr infra::Duration dacSettleTime = std::chrono::milliseconds(10);
        constexpr infra::Duration sampleInterval = std::chrono::milliseconds(50);
        constexpr infra::Duration waveInterval = std::chrono::milliseconds(20);
        constexpr uint16_t waveStep = 64;
    }

    AnalogMonitor::AnalogMonitor(hal::AdcStm& adc, hal::DigitalToAnalogPinImplBase& output, const infra::Function<void(uint16_t counts)>& onPotentiometer, const infra::Function<void(uint16_t counts)>& onDac)
        : potentiometer(adc, ADC_CHANNEL_0, InputConfig())
        , loopbackInput(adc, ADC_CHANNEL_1, InputConfig())
        , output(output)
        , onPotentiometer(onPotentiometer)
        , onDac(onDac)
    {
        StartWave();

        sampleTimer.Start(sampleInterval, [this]()
            {
                SamplePotentiometer();
            });
    }

    void AnalogMonitor::SetOutput(uint16_t value)
    {
        waveTimer.Cancel();
        Apply(value);
        services::GlobalTracer().Trace() << "dac " << static_cast<uint32_t>(value);
    }

    void AnalogMonitor::StartWave()
    {
        waveTimer.Start(waveInterval, [this]()
            {
                StepWave();
            });
    }

    void AnalogMonitor::MeasureInput()
    {
        if (!Claim())
            return;

        loopbackInput.Measure(1, [this](infra::MemoryRange<uint16_t> samples)
            {
                services::GlobalTracer().Trace() << "adc PA1_C " << static_cast<uint32_t>(samples.front()) << " of " << static_cast<uint32_t>(dacMaximum);
                Release();
            });
    }

    void AnalogMonitor::Loopback()
    {
        if (!Claim())
            return;

        waveTimer.Cancel();
        level = 0;
        Apply(loopbackLevels[level]);
        settleTimer.Start(dacSettleTime, [this]()
            {
                LoopbackApplied();
            });
    }

    hal::AnalogToDigitalChannelStm::Config AnalogMonitor::InputConfig()
    {
        hal::AnalogToDigitalChannelStm::Config config;
        config.samplingTime = ADC_SAMPLETIME_387CYCLES_5;
        return config;
    }

    bool AnalogMonitor::Claim()
    {
        if (busy)
        {
            services::GlobalTracer().Trace() << "analog measurement in progress";
            return false;
        }

        busy = true;
        return true;
    }

    void AnalogMonitor::Release()
    {
        busy = false;
    }

    void AnalogMonitor::SamplePotentiometer()
    {
        if (busy)
            return;

        busy = true;
        potentiometer.Measure(1, [this](infra::MemoryRange<uint16_t> samples)
            {
                Release();
                onPotentiometer(samples.front());
            });
    }

    void AnalogMonitor::StepWave()
    {
        if (rising)
        {
            waveValue = static_cast<uint16_t>(std::min<uint32_t>(waveValue + waveStep, dacMaximum));
            rising = waveValue != dacMaximum;
        }
        else
        {
            waveValue = waveValue > waveStep ? static_cast<uint16_t>(waveValue - waveStep) : 0;
            rising = waveValue == 0;
        }

        Apply(waveValue);
    }

    void AnalogMonitor::Apply(uint16_t value)
    {
        applied = value;
        output.Set(value);
        onDac(value);
    }

    void AnalogMonitor::LoopbackApplied()
    {
        loopbackInput.Measure(1, [this](infra::MemoryRange<uint16_t> samples)
            {
                services::GlobalTracer().Trace() << "dac " << static_cast<uint32_t>(applied) << " adc PA1_C " << static_cast<uint32_t>(samples.front()) << " difference " << static_cast<int32_t>(samples.front()) - static_cast<int32_t>(applied);

                if (++level != loopbackLevels.size())
                {
                    Apply(loopbackLevels[level]);
                    settleTimer.Start(dacSettleTime, [this]()
                        {
                            LoopbackApplied();
                        });
                    return;
                }

                Release();
                StartWave();
            });
    }
}
