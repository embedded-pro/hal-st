#include "boards/mb1166/Mb1166Setup.hpp"
#include "examples/display_demo/DisplayDemo.hpp"
#include "examples/stm32h757i_eval/DefaultClockEvalH757I.hpp"
#include "examples/stm32h757i_eval/EvalTracerInfrastructure.hpp"
#include "examples/stm32h757i_eval/EvalUi.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/Dma2dStm.hpp"
#include "hal_st/stm32fxxx/DsiHostStm.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/LtdcStm.hpp"
#include "infra/timer/Timer.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <array>
#include <chrono>

unsigned int hse_value = 25'000'000;

namespace
{
    // The frame buffers are in the AXI SRAM, which is too small for a full 800 x 480 screen,
    // so the demo draws in a window in the middle of it, with an overlay across the top of that window
    constexpr hal::DisplayTiming lcdTiming{ 27'500'000, boards::mb1166Panel.size, 34, 2, 34, 16, 1, 15 };
    constexpr hal::DsiHostStm::Pll dsiPll{ 25'000'000, 5, 100, 1 };

    constexpr hal::DisplayArea demoWindow{ 240, 140, 320, 200 };
    constexpr hal::DisplayArea demoOverlay{ 320, 120, 160, 80 };
    constexpr std::size_t frameMemorySize = 2 * 320 * 200 * 2 + 160 * 80 * 4;

    hal::DsiHostStm::Config DsiConfig()
    {
        hal::DsiHostStm::Config config;
        config.video.colorCoding = hal::DsiHostStm::ColorCoding::rgb565;
        return config;
    }
}

int main()
{
    const bool cortexM4Stopped = hal::WaitForCortexM4Stop();

    HAL_Init();
    ConfigureDefaultClockEvalH757I();
    ConfigureLtdcClockEvalH757I();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::EvalH757Ui ui;
    static services::DebugLed debugLed(ui.ledGreen);
    static main_::EvalH757TracerInfrastructure tracerInfrastructure;

    services::SetGlobalTracerInstance(tracerInfrastructure.tracer);

    if (cortexM4Stopped)
        hal::ReleaseCortexM4();
    else
        tracerInfrastructure.tracer.Trace() << "Cortex-M4 did not enter stop mode, not released";

    // MB1246: the 4" KoD KM-040TMP-02-0621 panel (OTM8009A) on the DSI host; reset on PF10, backlight on PA6
    alignas(32) static std::array<uint8_t, frameMemorySize> frameMemory;
    static hal::GpioPinStm lcdResetPin{ hal::Port::F, 10 };
    static hal::GpioPinStm lcdBacklightPin{ hal::Port::A, 6 };
    static hal::OutputPin lcdBacklight{ lcdBacklightPin };

    static hal::LtdcStm ltdc{ lcdTiming, infra::MemoryRange<const hal::LtdcStm::SignalPin>() };
    static hal::Dma2dStm dma2d;
    static hal::DsiHostStm dsi{ dsiPll, lcdTiming, DsiConfig() };
    static examples::DisplayDemo demo{ ltdc, dma2d, frameMemory, examples::DisplayDemo::Config{ demoWindow, hal::SurfaceFormat::rgb565, demoOverlay } };

    static boards::Mb1166Setup panel{ dsi, dsi, lcdResetPin, hal::PixelFormat::rgb565, [](drivers::MipiDsiPanelCore::InitializationResult result)
        {
            if (result == drivers::MipiDsiPanelCore::InitializationResult::success)
            {
                lcdBacklight.Set(true);
                demo.Start();
            }
            else
                services::GlobalTracer().Trace() << "Display panel did not initialize";
        } };

    static infra::TimerRepeating timerRepeating{ std::chrono::seconds{ 1 }, []
        {
            services::GlobalTracer().Trace() << "Hello World !";
        } };

    eventInfrastructure.Run();
    __builtin_unreachable();
}
