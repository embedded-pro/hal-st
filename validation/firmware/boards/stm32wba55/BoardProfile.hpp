#pragma once

#include "hal_st/stm32fxxx/DefaultClockNucleoWBA55CG.hpp"
#include "services/hil/HilArguments.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

namespace validation::board
{
    using hal::Port;

    inline constexpr const char* name = "NUCLEO-WBA55CG";
    inline constexpr const char* family = "stm32wba55";

    inline constexpr const char* portLetters = "ABCH";
    inline constexpr uint8_t maximumPinIndex = 15;
    static_assert(static_cast<uint8_t>(Port::H) == 3, "portLetters must follow hal::Port");

    inline constexpr std::array<uint16_t, 4> bondedPins{ { 0xffef, 0xffff, 0xe000, 0x0008 } };

    inline constexpr UartPins terminal{ 1, false, Pin(Port::B, 12), Pin(Port::A, 8) };
    inline constexpr uint32_t terminalBaudRate = 921600;
    inline constexpr DmaRequests terminalDma{ GPDMA1_REQUEST_USART1_TX, GPDMA1_REQUEST_USART1_RX };

    // Red LD3: the blue LD1 is the SPI1 clock pin, and the green LD2 needs a solder bridge on some board revisions
    inline constexpr HilPinId debugLed = Pin(Port::B, 8);
    inline constexpr std::array<HilPinId, 8> reservedPins{ {
        terminal.tx,
        terminal.rx,
        debugLed,
        Pin(Port::A, 13),
        Pin(Port::A, 14),
        Pin(Port::C, 14),
        Pin(Port::C, 15),
        Pin(Port::H, 3),
    } };

    inline constexpr std::optional<UartPins> defaultUart = UartPins{ 1, true, Pin(Port::B, 5), Pin(Port::A, 10) };
    inline constexpr QeiPins defaultQei{ 1, Pin(Port::A, 11), Pin(Port::A, 12), Pin(Port::A, 15) };

    inline constexpr auto aliases = std::to_array<HilPinAlias>({
        { "terminaltx", terminal.tx },
        { "terminalrx", terminal.rx },
        { "ain2", Pin(Port::A, 7) },
        { "ain3", Pin(Port::A, 6) },
        { "ain4", Pin(Port::A, 5) },
        { "ain7", Pin(Port::A, 2) },
        { "ain8", Pin(Port::A, 1) },
        { "ain9", Pin(Port::A, 0) },
        { "ain10", Pin(Port::B, 9) },
        { "tim1ch1", defaultQei.a },
        { "tim1ch2", defaultQei.b },
        { "tim1ch3", Pin(Port::B, 4) },
        { "tim1ch4", Pin(Port::B, 3) },
        { "tim1ch1n", Pin(Port::B, 2) },
        { "tim1ch2n", Pin(Port::B, 1) },
        { "tim1ch3n", Pin(Port::B, 0) },
        { "tim1bkin", Pin(Port::A, 2) },
        { "tim2ch1", Pin(Port::A, 5) },
        { "tim2ch3", Pin(Port::A, 7) },
        { "tim2ch4", Pin(Port::A, 6) },
        { "tim3ch1", Pin(Port::A, 10) },
        { "tim3ch2", Pin(Port::A, 1) },
        { "tim3ch3", Pin(Port::B, 14) },
        { "tim3ch4", Pin(Port::B, 9) },
        { "tim16ch1", Pin(Port::B, 9) },
        { "tim17ch1", Pin(Port::A, 1) },
        { "tim17ch1n", Pin(Port::B, 3) },
        { "qei1a", defaultQei.a },
        { "qei1b", defaultQei.b },
        { "qei1idx", defaultQei.idx },
        { "qei3a", Pin(Port::A, 10) },
        { "qei3b", Pin(Port::A, 1) },
        { "spi1clk", Pin(Port::B, 4) },
        { "spi1miso", Pin(Port::B, 3) },
        { "spi1mosi", Pin(Port::A, 15) },
        { "spi1cs", Pin(Port::A, 12) },
        { "lpuart1tx", defaultUart->tx },
        { "lpuart1rx", defaultUart->rx },
        { "lpuart1rts", Pin(Port::B, 9) },
        { "lpuart1cts", Pin(Port::B, 15) },
        { "usart2tx", Pin(Port::B, 0) },
        { "usart2rx", Pin(Port::A, 11) },
        { "usart2rts", Pin(Port::B, 1) },
        { "usart2cts", Pin(Port::B, 2) },
        { "led0", Pin(Port::B, 4) },
        { "led1", Pin(Port::A, 9) },
        { "gpio0", Pin(Port::B, 14) },
        { "gpio1", Pin(Port::A, 5) },
        { "gpio2", Pin(Port::A, 0) },
        { "sw1", Pin(Port::C, 13) },
        { "sw2", Pin(Port::B, 6) },
        { "sw3", Pin(Port::B, 7) },
    });

    inline constexpr uint8_t uartDmaChannel = 3;
    inline constexpr uint8_t spiDmaChannel = 5;
    inline constexpr uint8_t adcDmaChannel = 7;

    constexpr std::optional<DmaRequests> UartDma(uint8_t index, bool lpuart)
    {
        if (lpuart)
            return index == 1 ? std::make_optional(DmaRequests{ GPDMA1_REQUEST_LPUART1_TX, GPDMA1_REQUEST_LPUART1_RX }) : std::nullopt;

        switch (index)
        {
            case 1:
                return DmaRequests{ GPDMA1_REQUEST_USART1_TX, GPDMA1_REQUEST_USART1_RX };
            case 2:
                return DmaRequests{ GPDMA1_REQUEST_USART2_TX, GPDMA1_REQUEST_USART2_RX };
            default:
                return std::nullopt;
        }
    }

    constexpr std::optional<DmaRequests> SpiDma(uint8_t index)
    {
        switch (index)
        {
            case 1:
                return DmaRequests{ GPDMA1_REQUEST_SPI1_TX, GPDMA1_REQUEST_SPI1_RX };
            case 3:
                return DmaRequests{ GPDMA1_REQUEST_SPI3_TX, GPDMA1_REQUEST_SPI3_RX };
            default:
                return std::nullopt;
        }
    }

    inline constexpr uint8_t adc = 4;
    inline constexpr uint8_t adcDmaRequest = GPDMA1_REQUEST_ADC4;
    inline constexpr std::array<uint8_t, 2> adcTriggerTimers{ { 1, 2 } };
    inline constexpr uint32_t adcDefaultSamplingTime = ADC_SAMPLETIME_3CYCLES_5;
    inline constexpr std::array<services::HilChoice<uint32_t>, 8> adcSamplingTimes{ {
        { "1.5", ADC_SAMPLETIME_1CYCLE_5 },
        { "3.5", ADC_SAMPLETIME_3CYCLES_5 },
        { "7.5", ADC_SAMPLETIME_7CYCLES_5 },
        { "12.5", ADC_SAMPLETIME_12CYCLES_5 },
        { "19.5", ADC_SAMPLETIME_19CYCLES_5 },
        { "39.5", ADC_SAMPLETIME_39CYCLES_5 },
        { "79.5", ADC_SAMPLETIME_79CYCLES_5 },
        { "814.5", ADC_SAMPLETIME_814CYCLES_5 },
    } };

    inline uint32_t UartKernelClock(uint8_t index, bool lpuart)
    {
        if (lpuart)
            return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_LPUART1);

        return HAL_RCCEx_GetPeriphCLKFreq(index == 1 ? RCC_PERIPHCLK_USART1 : RCC_PERIPHCLK_USART2);
    }

    inline uint32_t SpiKernelClock(uint8_t index)
    {
        return HAL_RCCEx_GetPeriphCLKFreq(index == 1 ? RCC_PERIPHCLK_SPI1 : RCC_PERIPHCLK_SPI3);
    }

    inline void InitializeClocks()
    {
        ConfigureDefaultClockNucleoWBA55CG();
    }
}
