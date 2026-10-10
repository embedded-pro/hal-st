#pragma once

#include "demo/common/InvertedGpioPin.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"

namespace main_
{
    struct DiscoveryH745Ui
    {
        hal::GpioPinStm buttonUser{ hal::Port::C, 13 };
        hal::GpioPinStm ledRedPin{ hal::Port::I, 13 };
        hal::GpioPinStm ledGreenPin{ hal::Port::J, 2 };
        hal::GpioPinStm ledArduino{ hal::Port::D, 3 };
        InvertedGpioPin ledRed{ ledRedPin };
        InvertedGpioPin ledGreen{ ledGreenPin };
    };
}
