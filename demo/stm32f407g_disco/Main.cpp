#include "demo/audio_demo/ToneDemo.hpp"
#include "demo/stm32f407g_disco/PdmDecimator.hpp"
#include "drivers/audio/cs43l22/Cs43l22.hpp"
#include "drivers/audio/cs43l22/Cs43l22BusAccessI2c.hpp"
#include "drivers/imu/common/SensorWithPolling.hpp"
#include "drivers/imu/lis3dsh/Lis3dshBusAccess.hpp"
#include "drivers/imu/lis3dsh/Lis3dshCore.hpp"
#include "drivers/microphones/mp45dt02/Mp45dt02.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/BackupRamStm.hpp"
#include "hal_st/stm32fxxx/DefaultClockDiscoveryF407G.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/I2cStm.hpp"
#include "hal_st/stm32fxxx/I2sInputStm.hpp"
#include "hal_st/stm32fxxx/I2sOutputStm.hpp"
#include "hal_st/stm32fxxx/PwmStm.hpp"
#include "hal_st/stm32fxxx/RandomDataGeneratorStm.hpp"
#include "hal_st/stm32fxxx/SpiMasterStmDma.hpp"
#include "hal_st/stm32fxxx/UniqueDeviceId.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/peripheral/DebouncedButton.hpp"
#include "services/peripheral/SpiMasterWithChipSelect.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include "services/tracer/TracerOnSeggerRttInfrastructure.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>

unsigned int hse_value = 8'000'000;

namespace
{
    constexpr std::size_t toneBufferSamples = 2048;
    constexpr std::size_t microphoneBufferSamples = 2048;
    constexpr uint32_t microphoneSampleRate = 16000;
    constexpr uint16_t codecMasterClockRatio = 256;
    constexpr uint8_t toneVolumePercent = 80;
    constexpr int32_t oneG = 9807;
    constexpr uint32_t backupMagic = 0xb007c0de;

    // PLLI2S input is 1 MHz (HSE / PLLM), so 258 / 3 gives 86 MHz. It serves both I2S2 and I2S3, which is why neither uses AudioClockSwitchStm
    void ConfigurePllI2s()
    {
        RCC_PeriphCLKInitTypeDef clock{};
        clock.PeriphClockSelection = RCC_PERIPHCLK_I2S;
        clock.PLLI2S.PLLI2SN = 258;
        clock.PLLI2S.PLLI2SR = 3;
        HAL_RCCEx_PeriphCLKConfig(&clock);
    }

    hal::DutyCycle Brightness(int32_t acceleration)
    {
        return hal::DutyCycle::FromRatio(static_cast<uint64_t>(std::clamp<int32_t>(acceleration, 0, oneG)), oneG);
    }

    class TiltLeds
    {
    public:
        explicit TiltLeds(infra::MemoryRange<const hal::PwmStm::ChannelConfig> channels)
            : pwm(4, channels, PwmConfig())
        {
            pwm.SetBaseFrequency(hal::Hertz{ 1000 });
            pwm.Start(hal::DutyCycle{}, hal::DutyCycle{}, hal::DutyCycle{}, hal::DutyCycle{});
        }

        void Update(drivers::Lis3dshCore::Accelerometer::Samples samples)
        {
            if (samples.size() < 3)
                return;

            x = samples[0].Value();
            y = samples[1].Value();
            z = samples[2].Value();

            pwm.Start(Brightness(y), Brightness(-x), Brightness(-y), Brightness(x));
        }

        void Report() const
        {
            services::GlobalTracer().Trace() << "accel x=" << x << " y=" << y << " z=" << z << " mm/s2";
        }

    private:
        static hal::PwmStm::Config PwmConfig()
        {
            hal::PwmStm::Config config;
            config.prescaler = 83;
            return config;
        }

    private:
        hal::PwmStm pwm;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
    };

    class MicrophoneMeter
    {
    public:
        explicit MicrophoneMeter(hal::AudioInput& microphone)
            : microphone(microphone)
        {}

        void Start()
        {
            microphone.Start(
                hal::AudioFormat{ microphoneSampleRate, 1 }, [this](hal::AudioInput::Samples samples)
                {
                    Accumulate(samples);
                },
                [this]()
                {
                    ++overruns;
                });
        }

        void Report()
        {
            const uint32_t rms = count != 0 ? static_cast<uint32_t>(std::sqrt(static_cast<double>(sumOfSquares) / count)) : 0;
            services::GlobalTracer().Trace() << "mic rms=" << rms << " peak=" << peak << " overruns=" << overruns;

            sumOfSquares = 0;
            count = 0;
            peak = 0;
        }

    private:
        void Accumulate(hal::AudioInput::Samples samples)
        {
            for (int16_t sample : samples)
            {
                sumOfSquares += static_cast<uint64_t>(static_cast<int32_t>(sample) * static_cast<int32_t>(sample));
                peak = std::max<uint32_t>(peak, static_cast<uint32_t>(std::abs(static_cast<int32_t>(sample))));
            }

            count += static_cast<uint32_t>(samples.size());
        }

    private:
        hal::AudioInput& microphone;
        uint64_t sumOfSquares = 0;
        uint32_t count = 0;
        uint32_t peak = 0;
        uint32_t overruns = 0;
    };

    class ToneButton
    {
    public:
        explicit ToneButton(examples::ToneDemo& tone)
            : tone(tone)
        {}

        void Pressed()
        {
            switch (state)
            {
                case State::stopped:
                    state = State::running;
                    tone.Start();
                    services::GlobalTracer().Trace() << "tone on";
                    break;
                case State::running:
                    state = State::stopping;
                    tone.Stop([this]()
                        {
                            state = State::stopped;
                            services::GlobalTracer().Trace() << "tone off";
                        });
                    break;
                case State::stopping:
                    break;
            }
        }

    private:
        enum class State : uint8_t
        {
            stopped,
            running,
            stopping
        };

    private:
        examples::ToneDemo& tone;
        State state = State::stopped;
    };

    void TraceBootReport(hal::BackupRamStm& backupRam, hal::RandomDataGeneratorStm& randomDataGenerator, uint32_t& randomWord)
    {
        auto words = backupRam.Get();

        if (words[0] != backupMagic)
        {
            words[0] = backupMagic;
            words[1] = 0;
        }

        words[1] = words[1] + 1;

        services::GlobalTracer().Trace() << "STM32F4DISCOVERY boot #" << static_cast<uint32_t>(words[1]) << ", unique id " << infra::AsHex(hal::UniqueDeviceId());

        randomDataGenerator.GenerateRandomData(infra::MakeByteRange(randomWord), [&randomWord]()
            {
                services::GlobalTracer().Trace() << "random " << infra::hex << randomWord;
            });
    }

    // The F407 has no factory calibration for the sensor, so this uses the datasheet typical values with the 3.0 V VDDA of the board
    int32_t TemperatureInTenthsOfDegrees(uint16_t counts)
    {
        constexpr int32_t vddaInMillivolts = 3000;
        constexpr int32_t v25InMillivolts = 760;
        const int32_t millivolts = static_cast<int32_t>(counts) * vddaInMillivolts / 4095;
        return (millivolts - v25InMillivolts) * 4 + 250;
    }
}

int main()
{
    HAL_Init();
    ConfigureDefaultClockDiscoveryF407G();
    ConfigurePllI2s();

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::TracerOnSeggerRttInfrastructure tracerInfrastructure;

    static hal::BackupRamStm backupRam;
    static hal::RandomDataGeneratorStm randomDataGenerator;
    static uint32_t randomWord = 0;
    TraceBootReport(backupRam, randomDataGenerator, randomWord);

    static hal::DmaStm dma;
    static hal::DmaStm::TransmitStream accelerometerTransmitStream{ dma, hal::DmaChannelId{ 2, 3, 3 } };
    static hal::DmaStm::ReceiveStream accelerometerReceiveStream{ dma, hal::DmaChannelId{ 2, 2, 3 } };
    static hal::DmaStm::TransmitStream codecStream{ dma, hal::DmaChannelId{ 1, 7, 0 } };
    static hal::DmaStm::ReceiveStream microphoneStream{ dma, hal::DmaChannelId{ 1, 3, 0 } };

    static hal::GpioPinStm greenLed{ hal::Port::D, 12 };
    static hal::GpioPinStm orangeLed{ hal::Port::D, 13 };
    static hal::GpioPinStm redLed{ hal::Port::D, 14 };
    static hal::GpioPinStm blueLed{ hal::Port::D, 15 };
    static const std::array<hal::PwmStm::ChannelConfig, 4> ledChannels{ { { 1, greenLed }, { 2, orangeLed }, { 3, redLed }, { 4, blueLed } } };
    static TiltLeds tiltLeds{ infra::MemoryRange<const hal::PwmStm::ChannelConfig>(ledChannels) };

    static hal::GpioPinStm spiClockPin{ hal::Port::A, 5 };
    static hal::GpioPinStm spiMisoPin{ hal::Port::A, 6 };
    static hal::GpioPinStm spiMosiPin{ hal::Port::A, 7 };
    static hal::GpioPinStm accelerometerSelectPin{ hal::Port::E, 3 };
    static hal::SpiMasterStmDma accelerometerSpi{ accelerometerTransmitStream, accelerometerReceiveStream, 1, spiClockPin, spiMisoPin, spiMosiPin };
    static services::SpiMasterWithChipSelect accelerometerSpiWithSelect{ accelerometerSpi, accelerometerSelectPin };
    static drivers::Lis3dshBusAccessSpi accelerometerBus{ accelerometerSpiWithSelect };
    static drivers::SensorWithPolling<drivers::Lis3dshCore> accelerometer{ accelerometerBus };
    accelerometer.SetPollingInterval(std::chrono::milliseconds(10));
    accelerometer.Initialize(drivers::Lis3dshCore::Config{}, [](drivers::Lis3dshCore::InitializationResult result)
        {
            if (result != drivers::Lis3dshCore::InitializationResult::success)
            {
                services::GlobalTracer().Trace() << "LIS3DSH not found";
                return;
            }

            accelerometer.AsAccelerometer().Start([](drivers::Lis3dshCore::Accelerometer::Samples samples)
                {
                    tiltLeds.Update(samples);
                });
        });

    static hal::GpioPinStm codecSclPin{ hal::Port::B, 6 };
    static hal::GpioPinStm codecSdaPin{ hal::Port::B, 9 };
    static hal::GpioPinStm codecResetPin{ hal::Port::D, 4 };
    static hal::I2cStm::Config codecI2cConfig = []
    {
        hal::I2cStm::Config config;
        config.clockSpeed = 100000;
        return config;
    }();
    static hal::I2cStm codecI2c{ 1, codecSclPin, codecSdaPin, codecI2cConfig };

    static hal::GpioPinStm codecSdPin{ hal::Port::C, 12 };
    static hal::GpioPinStm codecSckPin{ hal::Port::C, 10 };
    static hal::GpioPinStm codecWsPin{ hal::Port::A, 4 };
    static hal::GpioPinStm codecMclkPin{ hal::Port::C, 7 };
    static hal::I2sOutputStm::Config codecI2sConfig = []
    {
        hal::I2sOutputStm::Config config;
        config.mclkOutput = true;
        return config;
    }();
    static hal::I2sOutputStm::WithBuffer<toneBufferSamples> codecI2s{ 3, codecStream, codecSdPin, codecSckPin, codecWsPin, codecMclkPin, codecI2sConfig };

    static drivers::Cs43l22BusAccessI2c codecBus{ codecI2c };
    static drivers::Cs43l22 codec{ codecBus, codecI2s, codecResetPin, drivers::Cs43l22::Config{ drivers::Cs43l22::Output::headphone, toneVolumePercent, codecMasterClockRatio, std::nullopt } };
    static examples::ToneDemo::Config toneConfig = []
    {
        examples::ToneDemo::Config config;
        config.volumePercent = toneVolumePercent;
        return config;
    }();
    static examples::ToneDemo tone{ codec, toneConfig };
    static ToneButton toneButton{ tone };

    static hal::GpioPinStm userButtonPin{ hal::Port::A, 0 };
    static services::DebouncedButton userButton{ userButtonPin, []()
        {
            toneButton.Pressed();
        } };

    static hal::GpioPinStm microphoneSdPin{ hal::Port::C, 3 };
    static hal::GpioPinStm microphoneCkPin{ hal::Port::B, 10 };
    static hal::I2sInputStm::Config microphoneI2sConfig = []
    {
        hal::I2sInputStm::Config config;
        config.mode = hal::I2sInputStm::Config::Mode::pdm;
        return config;
    }();
    static hal::I2sInputStm::WithBuffer<microphoneBufferSamples> microphoneI2s{ 2, microphoneStream, microphoneSdPin, microphoneCkPin, hal::dummyPinStm, hal::dummyPinStm, microphoneI2sConfig };
    static examples::PdmDecimator decimator;
    static std::array<int16_t, microphoneBufferSamples / 2 * 16 / examples::PdmDecimator::decimationFactor> microphonePeriod;
    static drivers::Mp45dt02 microphone{ microphoneI2s, decimator, infra::MakeRange(microphonePeriod) };
    static MicrophoneMeter microphoneMeter{ microphone };
    microphoneMeter.Start();

    static hal::AdcStm adc{ 1 };
    static hal::AnalogToDigitalInternalTemperatureStm::Config temperatureConfig = []
    {
        hal::AnalogToDigitalInternalTemperatureStm::Config config;
        config.samplingTime = ADC_SAMPLETIME_480CYCLES;
        return config;
    }();
    static hal::AnalogToDigitalInternalTemperatureStm temperatureSensor{ adc, temperatureConfig };

    static infra::TimerRepeating temperatureTimer{ std::chrono::seconds(2), []()
        {
            temperatureSensor.Measure(1, [](infra::MemoryRange<uint16_t> samples)
                {
                    const int32_t tenths = TemperatureInTenthsOfDegrees(samples.front());
                    services::GlobalTracer().Trace() << "temperature " << tenths / 10 << "." << std::abs(tenths % 10) << " C";
                });
        } };

    static infra::TimerRepeating microphoneTimer{ std::chrono::milliseconds(500), []()
        {
            microphoneMeter.Report();
        } };

    static infra::TimerRepeating accelerometerTimer{ std::chrono::seconds(1), []()
        {
            tiltLeds.Report();
        } };

    static uint32_t reportedUnderruns = 0;
    static infra::TimerRepeating underrunTimer{ std::chrono::milliseconds(200), []()
        {
            if (tone.Underruns() != reportedUnderruns)
            {
                reportedUnderruns = tone.Underruns();
                services::GlobalTracer().Trace() << "tone underruns=" << reportedUnderruns;
            }
        } };

    eventInfrastructure.Run();
    __builtin_unreachable();
}
