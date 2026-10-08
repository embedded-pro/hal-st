#include "drivers/audio/wm8994/Wm8994.hpp"
#include "drivers/audio/wm8994/Wm8994BusAccessI2c.hpp"
#include "examples/audio_demo/ToneDemo.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/AudioClockSwitchStm.hpp"
#include "hal_st/stm32fxxx/DefaultClockDiscoveryF746G.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/I2cStm.hpp"
#include "hal_st/stm32fxxx/SaiOutputStm.hpp"
#include "hal_st/stm32fxxx/SaiStm.hpp"
#include "infra/timer/Timer.hpp"
#include <chrono>

unsigned int hse_value = 25'000'000;

namespace
{
    constexpr uint8_t codecInitialVolumePercent = 70;
    constexpr std::size_t audioBufferSamples = 2048;

    // The PLLI2S input is 1 MHz (HSE / PLLM), so the SAI kernel clock is N / Q / DIVQ MHz
    RCC_PeriphCLKInitTypeDef Sai2KernelClock(uint32_t n, uint32_t q, uint32_t divQ)
    {
        RCC_PeriphCLKInitTypeDef clock{};
        clock.PeriphClockSelection = RCC_PERIPHCLK_SAI2;
        clock.Sai2ClockSelection = RCC_SAI2CLKSOURCE_PLLI2S;
        clock.PLLI2S.PLLI2SN = n;
        clock.PLLI2S.PLLI2SQ = q;
        clock.PLLI2SDivQ = divQ;
        return clock;
    }
}

int main()
{
    HAL_Init();
    ConfigureDefaultClockDiscoveryF746G();

    static main_::StmEventInfrastructure eventInfrastructure;

    static hal::GpioPinStm underrunLedPin{ hal::Port::I, 1 };
    static hal::OutputPin underrunLed{ underrunLedPin };

    static hal::GpioPinStm codecSclPin{ hal::Port::H, 7 };
    static hal::GpioPinStm codecSdaPin{ hal::Port::H, 8 };
    static hal::I2cStm codecI2c{ 3, codecSclPin, codecSdaPin };

    static hal::GpioPinStm saiMclkPin{ hal::Port::I, 4 };
    static hal::GpioPinStm saiSckPin{ hal::Port::I, 5 };
    static hal::GpioPinStm saiSdPin{ hal::Port::I, 6 };
    static hal::GpioPinStm saiFsPin{ hal::Port::I, 7 };

    // 49.143 MHz serves the 8, 16, 32, 48 and 96 kHz rates and 11.289 MHz the 11.025, 22.05 and 44.1 kHz ones
    static hal::AudioClockSwitchStm audioClock{ Sai2KernelClock(344, 7, 1), Sai2KernelClock(429, 2, 19) };

    static hal::DmaStm dma;
    static hal::DmaStm::TransmitStream saiTransmitStream{ dma, hal::DmaChannelId{ 2, 6, 3 } };

    static hal::SaiStm sai{ 2 };
    static hal::SaiOutputStm::Config saiConfig = []
    {
        hal::SaiOutputStm::Config config;
        config.audioClock = &audioClock;
        return config;
    }();
    static hal::SaiOutputStm::WithBuffer<audioBufferSamples> saiOutput{ sai, saiTransmitStream, saiSdPin, saiSckPin, saiFsPin, saiMclkPin, saiConfig };

    static drivers::Wm8994BusAccessI2c codecBus{ codecI2c };
    static drivers::Wm8994 codec{ codecBus, saiOutput, drivers::Wm8994::Config{ drivers::Wm8994::Output::headphone, codecInitialVolumePercent } };

    static examples::ToneDemo tone{ codec };
    tone.Start();

    static infra::TimerRepeating underrunTimer{ std::chrono::milliseconds(100), []()
        {
            underrunLed.Set(tone.Underruns() != 0);
        } };

    eventInfrastructure.Run();
    __builtin_unreachable();
}
