#include "demo/audio_demo/ToneDemo.hpp"
#include "demo/sd_card_demo/SdCardDemo.hpp"
#include "drivers/audio/wm8994/Wm8994.hpp"
#include "drivers/audio/wm8994/Wm8994BusAccessI2c.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/instantiations/StmTracerInfrastructure.hpp"
#include "hal_st/stm32fxxx/AudioClockSwitchStm.hpp"
#include "hal_st/stm32fxxx/DefaultClockDiscoveryF746G.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/I2cStm.hpp"
#include "hal_st/stm32fxxx/SaiOutputStm.hpp"
#include "hal_st/stm32fxxx/SaiStm.hpp"
#include "hal_st/stm32fxxx/SdCardStm.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <array>
#include <chrono>

unsigned int hse_value = 25'000'000;

namespace
{
    constexpr uint8_t codecInitialVolumePercent = 70;
    constexpr std::size_t audioBufferSamples = 2048;
    constexpr std::size_t sdBlockSize = 512;

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

    static hal::GpioPinStm traceUartTxPin{ hal::Port::A, 9 };
    static hal::GpioPinStm traceUartRxPin{ hal::Port::B, 7 };
    static main_::StmTracerInfrastructure tracerInfrastructure{ { 1, traceUartTxPin, traceUartRxPin } };
    services::SetGlobalTracerInstance(tracerInfrastructure.tracer);

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

    static hal::GpioPinStm sdClockPin{ hal::Port::C, 12 };
    static hal::GpioPinStm sdCommandPin{ hal::Port::D, 2 };
    static hal::GpioPinStm sdData0Pin{ hal::Port::C, 8 };
    static hal::GpioPinStm sdData1Pin{ hal::Port::C, 9 };
    static hal::GpioPinStm sdData2Pin{ hal::Port::C, 10 };
    static hal::GpioPinStm sdData3Pin{ hal::Port::C, 11 };

    static hal::SdCardStm sdCard{ 1, sdClockPin, sdCommandPin, sdData0Pin, sdData1Pin, sdData2Pin, sdData3Pin };

    alignas(32) static std::array<uint8_t, 3 * examples::SdCardDemo::scratchBlocks * sdBlockSize> sdBuffers;
    static examples::SdCardDemo sdDemo{ sdCard, infra::MakeRange(sdBuffers) };
    sdDemo.Start([](bool) {});

    eventInfrastructure.Run();
    __builtin_unreachable();
}
