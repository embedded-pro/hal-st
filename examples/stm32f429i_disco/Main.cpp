#include "boards/stm32f429i_disco_lcd/Stm32f429iDiscoLcdSetup.hpp"
#include "examples/display_demo/DisplayDemo.hpp"
#include "examples/stm32f429i_disco/DefaultClockDisco429I.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/Dma2dStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/LtdcStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "hal_st/stm32fxxx/SpiMasterStm.hpp"
#include "infra/timer/Timer.hpp"
#include "services/peripheral/DebugLed.hpp"
#include <array>
#include <chrono>

unsigned int hse_value = 8'000'000;

namespace
{
    constexpr hal::DisplayTiming lcdTiming{ 6'000'000, { 240, 320 }, 10, 10, 20, 4, 2, 2 };

    constexpr hal::DisplayArea demoWindow{ 0, 0, 240, 320 };
    constexpr hal::DisplayArea demoOverlay{ 70, 220, 100, 60 };
}

int main()
{
    HAL_Init();
    ConfigureDefaultClockDisco429I();
    ConfigureLtdcClockDisco429I();

    static main_::StmEventInfrastructure eventInfrastructure;

    static hal::GpioPinStm ledGreenPin{ hal::Port::G, 13 };
    static hal::GpioPinStm ledRedPin{ hal::Port::G, 14 };
    static services::DebugLed debugLed(ledGreenPin);
    static hal::OutputPin underrunLed{ ledRedPin };

    static hal::MultiGpioPinStm sdramPins{ hal::stm32f429discoveryFmcPins, hal::Drive::PushPull, hal::Speed::High };
    static hal::SdRamStm sdram{ sdramPins, hal::stm32f429discoverySdRamConfig };

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

    static hal::SpiMasterStm::Config spiConfig = []
    {
        hal::SpiMasterStm::Config config;
        config.baudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
        return config;
    }();
    static hal::SpiMasterStm spi{ 5, spiClockPin, spiMisoPin, spiMosiPin, spiConfig };

    static examples::DisplayDemo demo{ ltdc, dma2d, sdram.Memory(), examples::DisplayDemo::Config{ demoWindow, hal::SurfaceFormat::rgb565, demoOverlay } };

    static boards::Stm32f429iDiscoLcdSetup lcd{ spi, lcdChipSelectPin, lcdDataCommandPin, []()
        {
            demo.Start();
        } };

    static infra::TimerRepeating underrunTimer{ std::chrono::milliseconds(100), []()
        {
            underrunLed.Set(demo.Underruns() != 0);
        } };

    eventInfrastructure.Run();
    __builtin_unreachable();
}
