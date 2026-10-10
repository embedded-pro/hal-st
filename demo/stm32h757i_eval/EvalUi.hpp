#pragma once

#include "demo/common/InvertedGpioPin.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"

namespace main_
{
    // MB1246 evaluation board with H747XI, H757XI; the LEDs are active low
    struct EvalH757Ui
    {
        hal::GpioPinStm buttonWakeup{ hal::Port::A, 0 };
        hal::GpioPinStm buttonTamperPin{ hal::Port::C, 13 };
        InvertedGpioPin buttonTamper{ buttonTamperPin };
        hal::GpioPinStm ledGreenPin{ hal::Port::K, 3 };
        hal::GpioPinStm ledOrangePin{ hal::Port::K, 4 };
        hal::GpioPinStm ledRedPin{ hal::Port::K, 5 };
        hal::GpioPinStm ledBluePin{ hal::Port::K, 6 };
        InvertedGpioPin ledGreen{ ledGreenPin };
        InvertedGpioPin ledOrange{ ledOrangePin };
        InvertedGpioPin ledRed{ ledRedPin };
        InvertedGpioPin ledBlue{ ledBluePin };
    };
}
