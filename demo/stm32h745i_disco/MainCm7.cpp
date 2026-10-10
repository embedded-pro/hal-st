#include "demo/audio_demo/ToneDemo.hpp"
#include "demo/common/CountingI2cStm.hpp"
#include "demo/common/I2cScanner.hpp"
#include "demo/common/MemoryTests.hpp"
#include "demo/common/QuadSpiMemory.hpp"
#include "demo/common/TouchMonitor.hpp"
#include "demo/stm32h745i_disco/DefaultClockDiscoveryH745I.hpp"
#include "demo/stm32h745i_disco/DiscoveryDashboard.hpp"
#include "demo/stm32h745i_disco/DiscoveryH745Ui.hpp"
#include "demo/stm32h745i_disco/DiscoveryLcdPins.hpp"
#include "demo/stm32h745i_disco/DiscoveryMemories.hpp"
#include "drivers/audio/wm8994/Wm8994.hpp"
#include "drivers/audio/wm8994/Wm8994BusAccessI2c.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/Dma2dStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/LtdcStm.hpp"
#include "hal_st/stm32fxxx/QuadSpiStm.hpp"
#include "hal_st/stm32fxxx/SaiOutputStm.hpp"
#include "hal_st/stm32fxxx/SaiStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "hal_st/stm32fxxx/UartStm.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/peripheral/DebouncedButton.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "services/peripheral/I2cMultipleAccess.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/TracerWithDateTime.hpp"
#include "services/util/Terminal.hpp"
#include "services/util/TerminalWithStorage.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

unsigned int hse_value = 25'000'000;

namespace
{
    // The RK043FN48H panel runs from a 9.6 MHz pixel clock with active-low synchronisation and data enable. The data starts 43 clocks after the start of HSYNC, which is where ST's board support package puts it for this board, instead of the 54 of the older RK043FN48H timing
    constexpr hal::DisplayTiming lcdTiming{ 9'600'000, main_::DiscoveryDashboard::screenSize, 32, 41, 2, 2, 10, 2 };

    constexpr uint32_t secondsPerUptimeTrace = 10;
    constexpr std::size_t sdramPatternBytes = 2 * 1024 * 1024;
    constexpr std::size_t sdramRetestBytes = 4 * 1024 * 1024;
    constexpr std::size_t audioBufferSamples = 2048;
    constexpr uint8_t codecInitialVolumePercent = 70;
    constexpr infra::Duration qspiResponseTimeout = std::chrono::seconds(3);
    constexpr infra::Duration touchResetDuration = std::chrono::milliseconds(20);
    constexpr infra::Duration i2cScanDelay = std::chrono::milliseconds(500);
    constexpr infra::Duration touchStartDelay = std::chrono::milliseconds(600);

    // The controller reports portrait coordinates: its x runs along the short side of the landscape panel and its y along the long one
    drivers::Ft6x06::Config TouchConfig()
    {
        drivers::Ft6x06::Config config;
        config.size = hal::TouchScreenSize{ 272, 480 };
        config.orientation = drivers::Ft6x06::Orientation{ true, false, false };
        return config;
    }

    bool SupplyIsDirectSmps()
    {
        constexpr uint32_t supplyMask = PWR_CR3_SMPSEN | PWR_CR3_LDOEN | PWR_CR3_BYPASS;

        return (PWR->CR3 & supplyMask) == PWR_CR3_SMPSEN;
    }

    uint32_t Value(const volatile uint32_t& reg)
    {
        return reg;
    }

    void TraceDeviceState()
    {
        services::GlobalTracer().Trace() << "device " << infra::hex << HAL_GetDEVID() << ", revision " << HAL_GetREVID();
        services::GlobalTracer().Trace() << "clocks: sysclk " << HAL_RCC_GetSysClockFreq() / 1000000 << " MHz, hclk " << HAL_RCC_GetHCLKFreq() / 1000000 << " MHz, pclk1 " << HAL_RCC_GetPCLK1Freq() / 1000000 << " MHz";
        services::GlobalTracer().Trace() << "supply: " << (SupplyIsDirectSmps() ? "direct SMPS" : "NOT direct SMPS") << ", PWR_CR3 " << infra::hex << PWR->CR3 << ", PWR_CSR1 " << PWR->CSR1;
        services::GlobalTracer().Trace() << "reset flags: RCC_RSR " << infra::hex << RCC->RSR;
    }

    void TraceDisplayState(const main_::DiscoveryDashboard& dashboard)
    {
        services::GlobalTracer().Trace() << "display ltdc sscr " << infra::hex << Value(LTDC->SSCR) << " bpcr " << Value(LTDC->BPCR) << " awcr " << Value(LTDC->AWCR) << " twcr " << Value(LTDC->TWCR) << " gcr " << Value(LTDC->GCR) << " isr " << Value(LTDC->ISR) << " cdsr " << Value(LTDC->CDSR);
        services::GlobalTracer().Trace() << "display layer1 cr " << infra::hex << Value(LTDC_Layer1->CR) << " whpcr " << Value(LTDC_Layer1->WHPCR) << " wvpcr " << Value(LTDC_Layer1->WVPCR) << " pfcr " << Value(LTDC_Layer1->PFCR) << " cfbar " << Value(LTDC_Layer1->CFBAR) << " cfblr " << Value(LTDC_Layer1->CFBLR) << " cfblnr " << Value(LTDC_Layer1->CFBLNR);
        services::GlobalTracer().Trace() << "display rcc pll3divr " << infra::hex << Value(RCC->PLL3DIVR) << " cr " << Value(RCC->CR) << ", frames " << static_cast<uint32_t>(dashboard.FramesShown()) << ", underruns " << static_cast<uint32_t>(dashboard.Underruns());
    }

    // The first and the last 2 MB get the address-dependent pattern and the data bus and the address lines are walked over the whole memory; the retest leaves the frame buffer at the start alone
    std::size_t TestSdram(infra::ByteRange memory, bool boot)
    {
        if (!boot)
            return main_::TestMemory<uint16_t>(infra::Tail(memory, sdramRetestBytes), sdramRetestBytes);

        auto* tail = reinterpret_cast<volatile uint16_t*>(infra::Tail(memory, sdramPatternBytes).begin());

        return main_::TestMemory<uint16_t>(memory, sdramPatternBytes) + main_::TestPattern(tail, sdramPatternBytes / sizeof(uint16_t));
    }

    void ReportSdram(main_::DiscoveryDashboard& dashboard, const char* name, uint32_t kibibytes, std::size_t errors)
    {
        infra::StringOutputStream::WithStorage<24> detail;
        detail << kibibytes / 1024 << " MB";

        if (errors != 0)
            detail << " " << static_cast<uint32_t>(errors) << " ERR";

        dashboard.SetStatus(main_::DiscoveryDashboard::Item::sdram, errors == 0 ? main_::DiscoveryDashboard::State::ok : main_::DiscoveryDashboard::State::failed, detail.Storage());
        services::GlobalTracer().Trace() << name << " " << kibibytes << " KB: " << static_cast<uint32_t>(errors) << " errors";
    }
}

int main()
{
    const bool cortexM4Stopped = hal::WaitForCortexM4Stop();

    HAL_Init();
    ConfigureDefaultClockDiscoveryH745I();
    ConfigureLtdcClockDiscoveryH745I();
    ConfigureAudioClockDiscoveryH745I();

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

    static main_::DiscoveryLcdPins lcdPins;
    static hal::LtdcStm ltdc{ lcdTiming, lcdPins.Signals() };
    static hal::Dma2dStm dma2d;
    alignas(32) static std::array<uint8_t, main_::TouchCursor::bytes> cursorMemory;
    static main_::DiscoveryDashboard dashboard{ ltdc, dma2d, infra::Head(sdram.Memory(), main_::DiscoveryDashboard::frameBytes), infra::MakeRange(cursorMemory) };

    ReportSdram(dashboard, "SDRAM", static_cast<uint32_t>(sdram.Memory().size() / 1024), TestSdram(sdram.Memory(), true));

    static hal::GpioPinStm lcdDisplayEnablePin{ hal::Port::D, 7 };
    static hal::GpioPinStm lcdBacklightPin{ hal::Port::K, 0 };
    static hal::OutputPin lcdDisplayEnable{ lcdDisplayEnablePin, true };
    static hal::OutputPin lcdBacklight{ lcdBacklightPin, false };

    // The reset pin of the display connector also resets the touch controller
    static hal::GpioPinStm lcdResetPin{ hal::Port::B, 12 };
    static hal::OutputPin lcdReset{ lcdResetPin, false };
    static infra::TimerSingleShot lcdResetRelease{ touchResetDuration, []()
        {
            lcdReset.Set(true);
        } };

    static hal::GpioPinStm i2cSclPin{ hal::Port::D, 12 };
    static hal::GpioPinStm i2cSdaPin{ hal::Port::D, 13 };
    static main_::CountingI2cStm i2c{ 4, i2cSclPin, i2cSdaPin };
    static services::I2cMultipleAccessMaster i2cMaster{ i2c };
    static services::I2cMultipleAccess codecI2c{ i2cMaster };
    static services::I2cMultipleAccess touchI2c{ i2cMaster };
    static services::I2cMultipleAccess scanI2c{ i2cMaster };
    static main_::I2cScanner i2cScanner{ scanI2c };
    static infra::TimerSingleShot i2cScanStart{ i2cScanDelay, []()
        {
            i2cScanner.Scan();
        } };

    static main_::TouchMonitor touchMonitor{ touchI2c, TouchConfig(),
        [](bool found, uint8_t, uint8_t chipId)
        {
            infra::StringOutputStream::WithStorage<28> detail;

            if (found)
                detail << "FT5336 ID " << infra::hex << static_cast<uint32_t>(chipId);
            else
                detail << "NOT FOUND";

            dashboard.SetStatus(main_::DiscoveryDashboard::Item::touch, found ? main_::DiscoveryDashboard::State::ok : main_::DiscoveryDashboard::State::failed, detail.Storage());
        },
        [](const hal::TouchScreen::Event& event)
        {
            dashboard.SetTouch(event.phase, event.point);
            activityLed.Set(event.phase != hal::TouchScreen::Phase::released);
        } };

    static infra::TimerSingleShot touchStart{ touchStartDelay, []()
        {
            touchMonitor.Begin();
        } };

    static hal::GpioPinStm saiMclkPin{ hal::Port::I, 4 };
    static hal::GpioPinStm saiSckPin{ hal::Port::I, 5 };
    static hal::GpioPinStm saiSdPin{ hal::Port::I, 6 };
    static hal::GpioPinStm saiFsPin{ hal::Port::I, 7 };

    static hal::DmaStm dma;
    static hal::DmaStm::TransmitStream saiTransmitStream{ dma, hal::DmaChannelId{ 2, 1, DMA_REQUEST_SAI2_A } };
    static hal::SaiStm sai{ 2 };
    static hal::SaiOutputStm::WithBuffer<audioBufferSamples> saiOutput{ sai, saiTransmitStream, saiSdPin, saiSckPin, saiFsPin, saiMclkPin };
    static drivers::Wm8994BusAccessI2c codecBus{ codecI2c };
    static drivers::Wm8994 codec{ codecBus, saiOutput, drivers::Wm8994::Config{ drivers::Wm8994::Output::headphone, codecInitialVolumePercent } };
    static examples::ToneDemo tone{ codec };
    static bool muted = false;

    // The codec is brought up and the tone started once the display shows its first frame, which also switches the backlight on
    static auto displayStarted = []()
    {
        lcdBacklight.Set(true);
        dashboard.SetStatus(main_::DiscoveryDashboard::Item::display, main_::DiscoveryDashboard::State::ok, "LTDC 480X272 RGB565");
        tone.Start();
        dashboard.SetStatus(main_::DiscoveryDashboard::Item::audio, main_::DiscoveryDashboard::State::ok, "WM8994 440 HZ");
        services::GlobalTracer().Trace() << "display started, tone started";
    };

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
            infra::StringOutputStream::WithStorage<28> detail;
            detail << "ID " << infra::hex << qspiMemory.JedecId();
            dashboard.SetStatus(main_::DiscoveryDashboard::Item::qspi, ok ? main_::DiscoveryDashboard::State::ok : main_::DiscoveryDashboard::State::failed, detail.Storage());
            services::GlobalTracer().Trace() << "QSPI check " << (ok ? "ok" : "failed") << ", JEDEC " << infra::hex << qspiMemory.JedecId();
        } };

    static infra::TimerSingleShot qspiTimeout{ qspiResponseTimeout, []
        {
            if (!qspiMemory.Ready())
            {
                dashboard.SetStatus(main_::DiscoveryDashboard::Item::qspi, main_::DiscoveryDashboard::State::failed, "NO RESPONSE");
                services::GlobalTracer().Trace() << "QSPI flash did not answer";
            }
        } };

    static services::DebouncedButton userButton{ ui.buttonUser, []()
        {
            muted = !muted;
            codec.SetMuted(muted, []() {});
            dashboard.SetButton(true);
            activityLed.Set(true);
            services::GlobalTracer().Trace() << "user button, audio " << (muted ? "muted" : "unmuted");
        },
        []()
        {
            dashboard.SetButton(false);
            activityLed.Set(false);
        } };

    static infra::TimerRepeating secondTimer{ std::chrono::seconds{ 1 }, []
        {
            static uint32_t seconds = 0;

            dashboard.SetUptime(++seconds);

            if (seconds % secondsPerUptimeTrace == 0)
                services::GlobalTracer().Trace() << "uptime " << seconds << " s, frames " << static_cast<uint32_t>(dashboard.FramesShown()) << ", display underruns " << static_cast<uint32_t>(dashboard.Underruns()) << ", audio underruns " << tone.Underruns() << ", i2c nack " << i2c.NotAcknowledged() << " errors " << i2c.BusErrors();
        } };

    terminal.AddCommand({ { "info", "i", "print the device, clock and power supply state" }, [](const auto& params)
        {
            TraceDeviceState();
        } });

    terminal.AddCommand({ { "sdram", "sd", "retest the second half of the SDRAM, the first half holds the frame buffer" }, [](const auto& params)
        {
            ReportSdram(dashboard, "SDRAM retest", static_cast<uint32_t>(sdramRetestBytes / 1024), TestSdram(sdram.Memory(), false));
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
                    dashboard.SetStatus(main_::DiscoveryDashboard::Item::qspi, ok ? main_::DiscoveryDashboard::State::ok : main_::DiscoveryDashboard::State::failed, ok ? "WRITE TEST OK" : "WRITE TEST FAILED");
                    services::GlobalTracer().Trace() << "QSPI write test " << (ok ? "ok" : "failed");
                });
        } });

    terminal.AddCommand({ { "i2cscan", "i2c", "list the addresses on I2C4 that acknowledge" }, [](const auto& params)
        {
            i2cScanner.Scan();
        } });

    terminal.AddCommand({ { "display", "d", "print the LTDC, clock and frame counters" }, [](const auto& params)
        {
            TraceDisplayState(dashboard);
        } });

    terminal.AddCommand({ { "mute", "m", "toggle the audio mute" }, [](const auto& params)
        {
            muted = !muted;
            codec.SetMuted(muted, []() {});
        } });

    terminal.AddCommand({ { "clear", "c", "clear the touch pad" }, [](const auto& params)
        {
            dashboard.ClearTouchPad();
        } });

    dashboard.Start(displayStarted);

    services::GlobalTracer().Trace() << "type help for the commands";

    eventInfrastructure.Run();
    __builtin_unreachable();
}
