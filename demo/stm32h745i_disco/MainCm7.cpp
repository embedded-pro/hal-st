#include "demo/common/CountingI2cStm.hpp"
#include "demo/common/I2cScanner.hpp"
#include "demo/common/MemoryTests.hpp"
#include "demo/common/QuadSpiMemory.hpp"
#include "demo/stm32h745i_disco/DefaultClockDiscoveryH745I.hpp"
#include "demo/stm32h745i_disco/DiscoveryH745Ui.hpp"
#include "demo/stm32h745i_disco/DiscoveryMemories.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/I2cStm.hpp"
#include "hal_st/stm32fxxx/QuadSpiStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "hal_st/stm32fxxx/UartStm.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/peripheral/DebouncedButton.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/TracerWithDateTime.hpp"
#include "services/util/Terminal.hpp"
#include "services/util/TerminalWithStorage.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>

unsigned int hse_value = 25'000'000;

namespace
{
    constexpr uint32_t secondsPerUptimeTrace = 10;
    constexpr std::size_t sdramPatternBytes = 2 * 1024 * 1024;
    constexpr infra::Duration qspiResponseTimeout = std::chrono::seconds(3);
    constexpr infra::Duration touchResetDuration = std::chrono::milliseconds(20);
    constexpr infra::Duration i2cScanDelay = std::chrono::milliseconds(500);

    bool SupplyIsDirectSmps()
    {
        constexpr uint32_t supplyMask = PWR_CR3_SMPSEN | PWR_CR3_LDOEN | PWR_CR3_BYPASS;

        return (PWR->CR3 & supplyMask) == PWR_CR3_SMPSEN;
    }

    void TraceDeviceState()
    {
        services::GlobalTracer().Trace() << "device " << infra::hex << HAL_GetDEVID() << ", revision " << HAL_GetREVID();
        services::GlobalTracer().Trace() << "clocks: sysclk " << HAL_RCC_GetSysClockFreq() / 1000000 << " MHz, hclk " << HAL_RCC_GetHCLKFreq() / 1000000 << " MHz, pclk1 " << HAL_RCC_GetPCLK1Freq() / 1000000 << " MHz";
        services::GlobalTracer().Trace() << "supply: " << (SupplyIsDirectSmps() ? "direct SMPS" : "NOT direct SMPS") << ", PWR_CR3 " << infra::hex << PWR->CR3 << ", PWR_CSR1 " << PWR->CSR1;
        services::GlobalTracer().Trace() << "reset flags: RCC_RSR " << infra::hex << RCC->RSR;
    }

    // The first and the last 2 MB get the address-dependent pattern; the data bus and the address lines are walked over the whole memory
    std::size_t TestSdram(infra::ByteRange memory)
    {
        auto* tail = reinterpret_cast<volatile uint16_t*>(infra::Tail(memory, sdramPatternBytes).begin());

        return main_::TestMemory<uint16_t>(memory, sdramPatternBytes) + main_::TestPattern(tail, sdramPatternBytes / sizeof(uint16_t));
    }

    void ReportSdram(infra::ByteRange memory, std::size_t errors)
    {
        services::GlobalTracer().Trace() << "SDRAM " << static_cast<uint32_t>(memory.size() / 1024) << " KB: " << static_cast<uint32_t>(errors) << " errors";
    }
}

int main()
{
    const bool cortexM4Stopped = hal::WaitForCortexM4Stop();

    HAL_Init();
    ConfigureDefaultClockDiscoveryH745I();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::DiscoveryH745Ui ui;
    static services::DebugLed debugLed(ui.ledGreen);
    static hal::OutputPin activityLed{ ui.ledRed };

    static hal::GpioPinStm uartTxPin{ hal::Port::B, 10 };
    static hal::GpioPinStm uartRxPin{ hal::Port::B, 11 };
    static hal::UartStm uart{ 3, uartTxPin, uartRxPin };

    // The boot report is written before the event dispatcher runs and the writer drops what does not fit
    static services::StreamWriterOnSerialCommunication::WithStorage<4096> streamWriter{ uart };
    static infra::TextOutputStream::WithErrorPolicy textOutputStream{ streamWriter };
    static services::TracerWithDateTime tracer{ textOutputStream };
    services::SetGlobalTracerInstance(tracer);

    static services::TerminalWithCommandsImpl::WithMaxQueueAndMaxHistory<> terminalWithCommands{ uart, tracer };
    static services::TerminalWithStorage::WithMaxSize<20> terminal{ terminalWithCommands, tracer };

    if (cortexM4Stopped)
        hal::ReleaseCortexM4();
    else
        tracer.Trace() << "Cortex-M4 did not enter stop mode, not released";

    tracer.Trace() << "STM32H745I-DISCO demo, Cortex-M7 at " << HAL_RCC_GetSysClockFreq() / 1000000 << " MHz";
    TraceDeviceState();

    static hal::MultiGpioPinStm fmcPins{ main_::discoveryFmcPins, hal::Drive::PushPull, hal::Speed::High };
    static hal::SdRamStm sdram{ fmcPins, main_::DiscoverySdRamConfig() };
    ReportSdram(sdram.Memory(), TestSdram(sdram.Memory()));

    // The reset pin of the display connector also resets the touch controller, which has to run before the I2C scan
    static hal::GpioPinStm lcdResetPin{ hal::Port::B, 12 };
    static hal::OutputPin lcdReset{ lcdResetPin, false };
    static infra::TimerSingleShot lcdResetRelease{ touchResetDuration, []()
        {
            lcdReset.Set(true);
        } };

    static hal::GpioPinStm i2cSclPin{ hal::Port::D, 12 };
    static hal::GpioPinStm i2cSdaPin{ hal::Port::D, 13 };
    static main_::CountingI2cStm i2c{ 4, i2cSclPin, i2cSdaPin };
    static main_::I2cScanner i2cScanner{ i2c };
    static infra::TimerSingleShot i2cScanStart{ i2cScanDelay, []()
        {
            i2cScanner.Scan();
        } };

    // The two flash chips share the clock and BK1_NCS: only the one on the bank 1 data lines is used here, the other does not see any command
    static hal::GpioPinStm qspiClockPin{ hal::Port::F, 10 };
    static hal::GpioPinStm qspiSelectPin{ hal::Port::G, 6 };
    static hal::GpioPinStm qspiData0Pin{ hal::Port::D, 11 };
    static hal::GpioPinStm qspiData1Pin{ hal::Port::F, 9 };
    static hal::GpioPinStm qspiData2Pin{ hal::Port::F, 7 };
    static hal::GpioPinStm qspiData3Pin{ hal::Port::F, 6 };
    static hal::QuadSpiStm::Config qspiConfig = []
    {
        hal::QuadSpiStm::Config config;
        config.prescaler = 3;
        config.flashSizeLog2 = 26;
        return config;
    }();
    static hal::QuadSpiStm qspi{ qspiClockPin, qspiSelectPin, qspiData0Pin, qspiData1Pin, qspiData2Pin, qspiData3Pin, qspiConfig };
    static main_::QuadSpiMemory qspiMemory{ qspi, [](bool ok)
        {
            services::GlobalTracer().Trace() << "QSPI check " << (ok ? "ok" : "failed") << ", JEDEC " << infra::hex << qspiMemory.JedecId();
        } };

    static infra::TimerSingleShot qspiTimeout{ qspiResponseTimeout, []
        {
            if (!qspiMemory.Ready())
                services::GlobalTracer().Trace() << "QSPI flash did not answer";
        } };

    static services::DebouncedButton userButton{ ui.buttonUser, []()
        {
            activityLed.Set(true);
            services::GlobalTracer().Trace() << "user button pressed";
        },
        []()
        {
            activityLed.Set(false);
            services::GlobalTracer().Trace() << "user button released";
        } };

    static infra::TimerRepeating secondTimer{ std::chrono::seconds{ 1 }, []
        {
            static uint32_t seconds = 0;

            if (++seconds % secondsPerUptimeTrace == 0)
                services::GlobalTracer().Trace() << "uptime " << seconds << " s, i2c nack " << i2c.NotAcknowledged() << " errors " << i2c.BusErrors();
        } };

    terminal.AddCommand({ { "info", "i", "print the device, clock and power supply state" }, [](const auto& params)
        {
            TraceDeviceState();
        } });

    terminal.AddCommand({ { "sdram", "sd", "repeat the SDRAM test" }, [](const auto& params)
        {
            ReportSdram(sdram.Memory(), TestSdram(sdram.Memory()));
        } });

    terminal.AddCommand({ { "qspi", "q", "read the QSPI flash identification and the start of the array twice" }, [](const auto& params)
        {
            qspiMemory.Check([](bool ok)
                {
                    services::GlobalTracer().Trace() << "QSPI check " << (ok ? "ok" : "failed");
                });
        } });

    terminal.AddCommand({ { "qspitest", "qt", "erase, program and verify the last sector of the first QSPI flash" }, [](const auto& params)
        {
            qspiMemory.EraseProgramVerify([](bool ok)
                {
                    services::GlobalTracer().Trace() << "QSPI write test " << (ok ? "ok" : "failed");
                });
        } });

    terminal.AddCommand({ { "i2cscan", "i2c", "list the addresses on I2C4 that acknowledge" }, [](const auto& params)
        {
            i2cScanner.Scan();
        } });

    services::GlobalTracer().Trace() << "type help for the commands";

    eventInfrastructure.Run();
    __builtin_unreachable();
}
