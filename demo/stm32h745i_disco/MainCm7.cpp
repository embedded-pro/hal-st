#include "demo/stm32h745i_disco/DefaultClockDiscoveryH745I.hpp"
#include "demo/stm32h745i_disco/DiscoveryH745Ui.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/UartStm.hpp"
#include "infra/timer/Timer.hpp"
#include "services/peripheral/DebouncedButton.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/TracerWithDateTime.hpp"
#include "services/util/Terminal.hpp"
#include "services/util/TerminalWithStorage.hpp"
#include <chrono>
#include <cstdint>

unsigned int hse_value = 25'000'000;

namespace
{
    constexpr uint32_t secondsPerUptimeTrace = 10;

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
                services::GlobalTracer().Trace() << "uptime " << seconds << " s";
        } };

    terminal.AddCommand({ { "info", "i", "print the device, clock and power supply state" }, [](const auto& params)
        {
            TraceDeviceState();
        } });

    services::GlobalTracer().Trace() << "type help for the commands";

    eventInfrastructure.Run();
    __builtin_unreachable();
}
