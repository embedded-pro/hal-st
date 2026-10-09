#include "boards/stm32f429i_disco_lcd/Stm32f429iDiscoLcdSetup.hpp"
#include "demo/display_demo/DisplayDemo.hpp"
#include "demo/stm32f429i_disco/DefaultClockDisco429I.hpp"
#include "drivers/imu/l3gd20/L3gd20BusAccessSpi.hpp"
#include "drivers/imu/l3gd20/L3gd20Core.hpp"
#include "drivers/imu/l3gd20/L3gd20WithPolling.hpp"
#include "drivers/touch_screen/stmpe811/Stmpe811.hpp"
#include "drivers/touch_screen/stmpe811/Stmpe811BusAccessI2c.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/BackupRamStm.hpp"
#include "hal_st/stm32fxxx/DigitalToAnalogPinStm.hpp"
#include "hal_st/stm32fxxx/Dma2dStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/I2cStm.hpp"
#include "hal_st/stm32fxxx/LtdcStm.hpp"
#include "hal_st/stm32fxxx/RandomDataGeneratorStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "hal_st/stm32fxxx/SpiMasterStmDma.hpp"
#include "hal_st/stm32fxxx/UartStmDma.hpp"
#include "hal_st/stm32fxxx/UniqueDeviceId.hpp"
#include "infra/stream/StringInputStream.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/peripheral/DebouncedButton.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "services/peripheral/SpiMasterWithChipSelect.hpp"
#include "services/peripheral/SpiMultipleAccess.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/TracerWithDateTime.hpp"
#include "services/util/Terminal.hpp"
#include "services/util/TerminalWithStorage.hpp"
#include "stm32f4xx_ll_adc.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

unsigned int hse_value = 8'000'000;

namespace
{
    constexpr hal::DisplayTiming lcdTiming{ 6'000'000, { 240, 320 }, 10, 10, 20, 4, 2, 2 };

    constexpr hal::DisplayArea demoWindow{ 0, 0, 240, 320 };
    constexpr hal::DisplayArea demoOverlay{ 70, 220, 100, 60 };

    constexpr uint32_t backupMagic = 0xb007c0de;
    constexpr std::size_t sdramTestBytes = 64 * 1024;

    // The MB1075 revision E and later carry an I3G4250D, the earlier ones an L3GD20; the register maps match and only WHO_AM_I differs
    constexpr std::array<uint8_t, 2> gyroscopeIdentifications{ 0xd3, 0xd4 };

    // The DAC output buffer cannot reach the rails, so the sweep stays clear of 0 and 4095
    constexpr std::array<uint16_t, 5> loopbackLevels{ 512, 1024, 2048, 3072, 3584 };
    constexpr uint32_t dacMaximum = 4095;
    constexpr infra::Duration dacSettleTime = std::chrono::milliseconds(10);

    // A moved event arrives every touch poll interval, which would take most of the serial link
    constexpr uint32_t movedEventsPerTrace = 10;

    bool Parse(infra::BoundedConstString params, uint32_t& value)
    {
        infra::StringInputStream stream{ params, infra::softFail };
        stream >> value;

        return !stream.Failed();
    }

    // A word-wide data bus walk, then a different value per address and its complement, so a stuck data line, a shorted address line or a missed refresh all show up
    std::size_t TestSdram(infra::ByteRange memory)
    {
        constexpr std::size_t wordCount = sdramTestBytes / sizeof(uint32_t);
        constexpr uint32_t addressSpread = 0x9e3779b1;
        constexpr auto expected = [](std::size_t index, uint32_t complement)
        {
            return (static_cast<uint32_t>(index) * addressSpread) ^ complement;
        };

        auto* words = reinterpret_cast<volatile uint32_t*>(infra::Tail(memory, sdramTestBytes).begin());
        std::size_t errors = 0;

        for (uint32_t bit = 0; bit != 32; ++bit)
        {
            words[0] = 1u << bit;

            if (words[0] != 1u << bit)
                ++errors;
        }

        for (uint32_t complement : { 0u, ~0u })
        {
            for (std::size_t index = 0; index != wordCount; ++index)
                words[index] = expected(index, complement);

            for (std::size_t index = 0; index != wordCount; ++index)
                if (words[index] != expected(index, complement))
                    ++errors;
        }

        return errors;
    }

    void TraceSdramTest(infra::ByteRange memory)
    {
        const std::size_t errors = TestSdram(memory);

        services::GlobalTracer().Trace() << "SDRAM " << static_cast<uint32_t>(memory.size() / 1024) << " KB, test of " << static_cast<uint32_t>(sdramTestBytes / 1024) << " KB: " << static_cast<uint32_t>(errors) << " errors";
    }

    // The factory calibration is taken at a VDDA of 3.3 V, so another supply voltage shifts the reading
    int32_t TemperatureInTenthsOfDegrees(uint16_t counts)
    {
        const int32_t counts30 = *TEMPSENSOR_CAL1_ADDR;
        const int32_t counts110 = *TEMPSENSOR_CAL2_ADDR;

        return 10 * TEMPSENSOR_CAL1_TEMP + 10 * (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) * (static_cast<int32_t>(counts) - counts30) / (counts110 - counts30);
    }

    void TraceBootReport(hal::BackupRamStm& backupRam, hal::RandomDataGeneratorStm& randomDataGenerator, uint32_t& randomWord)
    {
        auto words = backupRam.Get();

        if (words[0] != backupMagic)
        {
            words[0] = backupMagic;
            words[1] = 0;
        }

        words[1] = words[1] + 1;

        services::GlobalTracer().Trace() << "STM32F429I-DISC1 boot #" << static_cast<uint32_t>(words[1]) << ", unique id " << infra::AsHex(hal::UniqueDeviceId());
        services::GlobalTracer().Trace() << "flash " << static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(FLASHSIZE_BASE)) << " KB, revision " << infra::hex << HAL_GetREVID();

        randomDataGenerator.GenerateRandomData(infra::MakeByteRange(randomWord), [&randomWord]()
            {
                services::GlobalTracer().Trace() << "random " << infra::hex << randomWord;
            });
    }

    class GyroscopeMonitor
    {
    public:
        explicit GyroscopeMonitor(services::RegisterBusAccess& bus)
            : gyroscope(bus)
        {
            gyroscope.SetPollingInterval(std::chrono::milliseconds(10));
        }

        void Start()
        {
            Identify(0);
        }

        void Report() const
        {
            if (running)
                services::GlobalTracer().Trace() << "gyroscope x=" << x << " y=" << y << " z=" << z << " mdps, " << samplesReceived << " samples";
            else
                services::GlobalTracer().Trace() << "gyroscope not available";
        }

    private:
        using Device = drivers::L3gd20WithPolling<drivers::L3gd20Core>;

        void Identify(std::size_t candidate)
        {
            drivers::L3gd20Core::Config config;
            config.expectedIdentification = gyroscopeIdentifications[candidate];
            config.outputDataRate = drivers::L3gd20Core::OutputDataRate::hertz100;

            gyroscope.Initialize(config, [this, candidate](drivers::L3gd20Core::InitializationResult result)
                {
                    if (result == drivers::L3gd20Core::InitializationResult::success)
                        Begin(candidate);
                    else if (candidate + 1 != gyroscopeIdentifications.size())
                        Identify(candidate + 1);
                    else
                        services::GlobalTracer().Trace() << "gyroscope not found";
                });
        }

        void Begin(std::size_t candidate)
        {
            running = true;
            services::GlobalTracer().Trace() << "gyroscope found, WHO_AM_I " << infra::hex << static_cast<uint32_t>(gyroscopeIdentifications[candidate]);

            gyroscope.AsGyroscope().Start([this](drivers::L3gd20Core::Gyroscope::Samples samples)
                {
                    if (samples.size() < 3)
                        return;

                    x = samples[0].Value();
                    y = samples[1].Value();
                    z = samples[2].Value();
                    ++samplesReceived;
                });
        }

    private:
        Device gyroscope;
        bool running = false;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        uint32_t samplesReceived = 0;
    };

    class TouchMonitor
    {
    public:
        TouchMonitor(services::RegisterBusAccess& bus, hal::GpioPin& interruptPin)
            : touch(bus, interruptPin, drivers::Stmpe811::Config{}, [this](drivers::Stmpe811::InitializationResult result)
                  {
                      Initialized(result);
                  })
        {}

    private:
        void Initialized(drivers::Stmpe811::InitializationResult result)
        {
            if (result != drivers::Stmpe811::InitializationResult::success)
            {
                services::GlobalTracer().Trace() << "STMPE811 not found";
                return;
            }

            services::GlobalTracer().Trace() << "STMPE811 found";

            touch.Start([this](hal::TouchScreen::Event event)
                {
                    Report(event);
                });
        }

        void Report(const hal::TouchScreen::Event& event)
        {
            if (event.phase != hal::TouchScreen::Phase::moved)
                movedEvents = 0;
            else if (++movedEvents % movedEventsPerTrace != 0)
                return;

            services::GlobalTracer().Trace() << "touch " << PhaseName(event.phase) << " x=" << static_cast<uint32_t>(event.point.x) << " y=" << static_cast<uint32_t>(event.point.y);
        }

        static const char* PhaseName(hal::TouchScreen::Phase phase)
        {
            switch (phase)
            {
                case hal::TouchScreen::Phase::pressed:
                    return "pressed";
                case hal::TouchScreen::Phase::moved:
                    return "moved";
                case hal::TouchScreen::Phase::released:
                    return "released";
            }

            return "";
        }

    private:
        drivers::Stmpe811 touch;
        uint32_t movedEvents = 0;
    };

    // The ADC serves the die temperature sensor and the loopback input, and AdcStm takes one measurement at a time
    class AnalogMonitor
    {
    public:
        AnalogMonitor(hal::AdcStm& adc, hal::GpioPinStm& inputPin, hal::DigitalToAnalogPinImplBase& output)
            : temperatureSensor(adc, TemperatureConfig())
            , input(inputPin, adc, InputConfig())
            , output(output)
        {}

        void MeasureTemperature()
        {
            if (!Claim())
                return;

            temperatureSensor.Measure(1, [this](infra::MemoryRange<uint16_t> samples)
                {
                    busy = false;

                    const int32_t tenths = TemperatureInTenthsOfDegrees(samples.front());
                    services::GlobalTracer().Trace() << "die temperature " << tenths / 10 << "." << std::abs(tenths % 10) << " C (raw " << static_cast<uint32_t>(samples.front()) << ", calibration " << static_cast<uint32_t>(*TEMPSENSOR_CAL1_ADDR) << " at 30 C and " << static_cast<uint32_t>(*TEMPSENSOR_CAL2_ADDR) << " at 110 C)";
                });
        }

        void MeasureInput()
        {
            if (!Claim())
                return;

            input.Measure(1, [this](infra::MemoryRange<uint16_t> samples)
                {
                    busy = false;

                    services::GlobalTracer().Trace() << "adc " << static_cast<uint32_t>(samples.front()) << " of " << dacMaximum;
                });
        }

        void SetOutput(uint16_t value)
        {
            if (Claim())
                Apply(value);
        }

        void Loopback()
        {
            if (!Claim())
                return;

            sweeping = true;
            level = 0;
            Apply(loopbackLevels[level]);
        }

    private:
        static hal::AnalogToDigitalInternalTemperatureStm::Config TemperatureConfig()
        {
            hal::AnalogToDigitalInternalTemperatureStm::Config config;
            config.samplingTime = ADC_SAMPLETIME_480CYCLES;
            return config;
        }

        static hal::AnalogToDigitalPinImplStm::Config InputConfig()
        {
            hal::AnalogToDigitalPinImplStm::Config config;
            config.samplingTime = ADC_SAMPLETIME_56CYCLES;
            return config;
        }

        bool Claim()
        {
            if (busy)
            {
                services::GlobalTracer().Trace() << "analog measurement in progress";
                return false;
            }

            busy = true;
            return true;
        }

        void Apply(uint16_t value)
        {
            applied = value;
            output.Set(value);

            settle.Start(dacSettleTime, [this]()
                {
                    input.Measure(1, [this](infra::MemoryRange<uint16_t> samples)
                        {
                            Applied(samples.front());
                        });
                });
        }

        void Applied(uint16_t counts)
        {
            services::GlobalTracer().Trace() << "dac " << static_cast<uint32_t>(applied) << " adc " << static_cast<uint32_t>(counts) << " difference " << static_cast<int32_t>(counts) - static_cast<int32_t>(applied);

            if (sweeping && ++level != loopbackLevels.size())
                return Apply(loopbackLevels[level]);

            sweeping = false;
            busy = false;
        }

    private:
        hal::AnalogToDigitalInternalTemperatureStm temperatureSensor;
        hal::AnalogToDigitalPinImplStm input;
        hal::DigitalToAnalogPinImplBase& output;
        infra::TimerSingleShot settle;
        bool busy = false;
        bool sweeping = false;
        std::size_t level = 0;
        uint16_t applied = 0;
    };
}

int main()
{
    HAL_Init();
    ConfigureDefaultClockDisco429I();
    ConfigureLtdcClockDisco429I();

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    static main_::StmEventInfrastructure eventInfrastructure;

    static hal::DmaStm dma;
    static hal::GpioPinStm uartTxPin{ hal::Port::A, 9 };
    static hal::GpioPinStm uartRxPin{ hal::Port::A, 10 };
    static hal::DmaStm::TransmitStream uartTransmitStream{ dma, hal::DmaChannelId{ 2, 7, 4 } };
    static hal::UartStmDma uart{ uartTransmitStream, 1, uartTxPin, uartRxPin };

    // The boot report is written before the event dispatcher runs and the help table in one go, and the writer drops what does not fit, so the buffer must hold either
    static services::StreamWriterOnSerialCommunication::WithStorage<2048> streamWriter{ uart };
    static infra::TextOutputStream::WithErrorPolicy textOutputStream{ streamWriter };
    static services::TracerWithDateTime tracer{ textOutputStream };
    services::SetGlobalTracerInstance(tracer);

    static services::TerminalWithCommandsImpl::WithMaxQueueAndMaxHistory<> terminalWithCommands{ uart, tracer };
    static services::TerminalWithStorage::WithMaxSize<12> terminal{ terminalWithCommands, tracer };

    static hal::BackupRamStm backupRam;
    static hal::RandomDataGeneratorStm randomDataGenerator;
    static uint32_t randomWord = 0;
    TraceBootReport(backupRam, randomDataGenerator, randomWord);

    static hal::GpioPinStm ledGreenPin{ hal::Port::G, 13 };
    static hal::GpioPinStm ledRedPin{ hal::Port::G, 14 };
    static services::DebugLed debugLed(ledGreenPin);
    static hal::OutputPin underrunLed{ ledRedPin };

    static hal::MultiGpioPinStm sdramPins{ hal::stm32f429discoveryFmcPins, hal::Drive::PushPull, hal::Speed::High };
    static hal::SdRamStm sdram{ sdramPins, hal::stm32f429discoverySdRamConfig };

    TraceSdramTest(sdram.Memory());

    static hal::GpioPinStm pa3{ hal::Port::A, 3 };
    static hal::GpioPinStm pa4{ hal::Port::A, 4 };
    static hal::GpioPinStm pa6{ hal::Port::A, 6 };
    static hal::GpioPinStm pa11{ hal::Port::A, 11 };
    static hal::GpioPinStm pa12{ hal::Port::A, 12 };
    static hal::GpioPinStm pb0{ hal::Port::B, 0 };
    static hal::GpioPinStm pb1{ hal::Port::B, 1 };
    static hal::GpioPinStm pb8{ hal::Port::B, 8 };
    static hal::GpioPinStm pb9{ hal::Port::B, 9 };
    static hal::GpioPinStm pb10{ hal::Port::B, 10 };
    static hal::GpioPinStm pb11{ hal::Port::B, 11 };
    static hal::GpioPinStm pc6{ hal::Port::C, 6 };
    static hal::GpioPinStm pc7{ hal::Port::C, 7 };
    static hal::GpioPinStm pc10{ hal::Port::C, 10 };
    static hal::GpioPinStm pd3{ hal::Port::D, 3 };
    static hal::GpioPinStm pd6{ hal::Port::D, 6 };
    static hal::GpioPinStm pf10{ hal::Port::F, 10 };
    static hal::GpioPinStm pg6{ hal::Port::G, 6 };
    static hal::GpioPinStm pg7{ hal::Port::G, 7 };
    static hal::GpioPinStm pg10{ hal::Port::G, 10 };
    static hal::GpioPinStm pg11{ hal::Port::G, 11 };
    static hal::GpioPinStm pg12{ hal::Port::G, 12 };

    static std::array<hal::LtdcStm::SignalPin, 22> lcdSignals{ { { hal::PinConfigTypeStm::ltdcClk, pg7 },
        { hal::PinConfigTypeStm::ltdcHsync, pc6 },
        { hal::PinConfigTypeStm::ltdcVsync, pa4 },
        { hal::PinConfigTypeStm::ltdcDe, pf10 },
        { hal::PinConfigTypeStm::ltdcR2, pc10 },
        { hal::PinConfigTypeStm::ltdcR3, pb0 },
        { hal::PinConfigTypeStm::ltdcR4, pa11 },
        { hal::PinConfigTypeStm::ltdcR5, pa12 },
        { hal::PinConfigTypeStm::ltdcR6, pb1 },
        { hal::PinConfigTypeStm::ltdcR7, pg6 },
        { hal::PinConfigTypeStm::ltdcG2, pa6 },
        { hal::PinConfigTypeStm::ltdcG3, pg10 },
        { hal::PinConfigTypeStm::ltdcG4, pb10 },
        { hal::PinConfigTypeStm::ltdcG5, pb11 },
        { hal::PinConfigTypeStm::ltdcG6, pc7 },
        { hal::PinConfigTypeStm::ltdcG7, pd3 },
        { hal::PinConfigTypeStm::ltdcB2, pd6 },
        { hal::PinConfigTypeStm::ltdcB3, pg11 },
        { hal::PinConfigTypeStm::ltdcB4, pg12 },
        { hal::PinConfigTypeStm::ltdcB5, pa3 },
        { hal::PinConfigTypeStm::ltdcB6, pb8 },
        { hal::PinConfigTypeStm::ltdcB7, pb9 } } };

    static hal::LtdcStm ltdc{ lcdTiming, lcdSignals };
    static hal::Dma2dStm dma2d;

    static hal::GpioPinStm spiClockPin{ hal::Port::F, 7 };
    static hal::GpioPinStm spiMisoPin{ hal::Port::F, 8 };
    static hal::GpioPinStm spiMosiPin{ hal::Port::F, 9 };
    static hal::GpioPinStm lcdChipSelectPin{ hal::Port::C, 2 };
    static hal::GpioPinStm lcdDataCommandPin{ hal::Port::D, 13 };
    static hal::GpioPinStm gyroscopeChipSelectPin{ hal::Port::C, 1 };

    // SpiMasterStm ends the session before it delivers onDone, so SpiMultipleAccess would start the next client's transfer first and the late onDone would complete that one; the DMA master delivers onDone straight after ending the session
    static hal::DmaStm::TransmitStream spiTransmitStream{ dma, hal::DmaChannelId{ 2, 4, 2 } };
    static hal::DmaStm::ReceiveStream spiReceiveStream{ dma, hal::DmaChannelId{ 2, 3, 2 } };
    static hal::SpiMasterStmDma spi{ spiTransmitStream, spiReceiveStream, 5, spiClockPin, spiMisoPin, spiMosiPin };
    static services::SpiMultipleAccessMaster spiMaster{ spi };
    static services::SpiMultipleAccess lcdSpi{ spiMaster };
    static services::SpiMultipleAccess gyroscopeSpi{ spiMaster };

    static examples::DisplayDemo demo{ ltdc, dma2d, sdram.Memory(), examples::DisplayDemo::Config{ demoWindow, hal::SurfaceFormat::rgb565, demoOverlay } };

    static boards::Stm32f429iDiscoLcdSetup lcd{ lcdSpi, lcdChipSelectPin, lcdDataCommandPin, []()
        {
            demo.Start();
        } };

    static services::SpiMasterWithChipSelect gyroscopeSpiWithChipSelect{ gyroscopeSpi, gyroscopeChipSelectPin };
    static drivers::L3gd20BusAccessSpi gyroscopeBus{ gyroscopeSpiWithChipSelect };
    static GyroscopeMonitor gyroscopeMonitor{ gyroscopeBus };
    gyroscopeMonitor.Start();

    static hal::GpioPinStm touchSclPin{ hal::Port::A, 8 };
    static hal::GpioPinStm touchSdaPin{ hal::Port::C, 9 };
    static hal::GpioPinStm touchInterruptPin{ hal::Port::A, 15 };
    static hal::I2cStm::Config touchI2cConfig = []
    {
        hal::I2cStm::Config config;
        config.clockSpeed = 100000;
        return config;
    }();
    static hal::I2cStm touchI2c{ 3, touchSclPin, touchSdaPin, touchI2cConfig };
    static drivers::Stmpe811BusAccessI2c touchBus{ touchI2c };
    static TouchMonitor touchMonitor{ touchBus, touchInterruptPin };

    static hal::GpioPinStm dacPin{ hal::Port::A, 5 };
    static hal::GpioPinStm adcPin{ hal::Port::C, 3 };
    static hal::DacStm dac{ 1 };
    static hal::DigitalToAnalogPinImplStm dacOutput{ dacPin, dac };
    static hal::AdcStm adc{ 1 };
    static AnalogMonitor analogMonitor{ adc, adcPin, dacOutput };

    static hal::GpioPinStm userButtonPin{ hal::Port::A, 0 };
    static services::DebouncedButton userButton{ userButtonPin, []()
        {
            services::GlobalTracer().Trace() << "user button";
            gyroscopeMonitor.Report();
            analogMonitor.MeasureTemperature();
        } };

    static infra::TimerRepeating underrunTimer{ std::chrono::milliseconds(100), []()
        {
            underrunLed.Set(demo.Underruns() != 0);
        } };

    terminal.AddCommand({ { "gyroscope", "gyro", "show the latest angular rate" }, [](const auto& params)
        {
            gyroscopeMonitor.Report();
        } });

    terminal.AddCommand({ { "temperature", "temp", "measure the die temperature" }, [](const auto& params)
        {
            analogMonitor.MeasureTemperature();
        } });

    terminal.AddCommand({ { "adc", "a", "measure the ADC input on PC3" }, [](const auto& params)
        {
            analogMonitor.MeasureInput();
        } });

    terminal.AddCommand({ { "dac", "d", "set the DAC output on PA5 and read it back on PC3, 0 to 4095", "<value>" }, [](const auto& params)
        {
            uint32_t value = 0;
            if (!Parse(params, value) || value > dacMaximum)
            {
                services::GlobalTracer().Trace() << "dac: invalid parameter";
                return;
            }

            analogMonitor.SetOutput(static_cast<uint16_t>(value));
        } });

    terminal.AddCommand({ { "loopback", "loop", "sweep the DAC and read each level back on PC3" }, [](const auto& params)
        {
            analogMonitor.Loopback();
        } });

    terminal.AddCommand({ { "sdram", "sd", "repeat the SDRAM test" }, [](const auto& params)
        {
            TraceSdramTest(sdram.Memory());
        } });

    services::GlobalTracer().Trace() << "type help for the commands";

    eventInfrastructure.Run();
    __builtin_unreachable();
}
