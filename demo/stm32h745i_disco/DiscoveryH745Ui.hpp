#pragma once

#include "demo/common/InvertedGpioPin.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"

namespace main_
{
    // MB1381 discovery board with H745XI; LD6 and LD7 are wired to 3.3 V and so are active low, LD8 (Arduino LED, switched by a transistor) and the user button are active high
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
