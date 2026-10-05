#pragma once

#include "hal_st/stm32fxxx/DefaultClockNucleoWB55RG.hpp"
#include "services/hil/HilArguments.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

namespace validation::board
{
    using hal::Port;

    inline constexpr const char* name = "NUCLEO-WB55RG";
    inline constexpr const char* family = "stm32wb55";

    inline constexpr const char* portLetters = "ABCDEH";
    inline constexpr uint8_t maximumPinIndex = 15;
    static_assert(static_cast<uint8_t>(Port::H) == 5, "portLetters must follow hal::Port");

    // The generated pinout table also lists pins of larger packages; only these are bonded out on the VFQFPN68
    inline constexpr std::array<uint16_t, 6> bondedPins{ { 0xffff, 0xffff, 0xfc7f, 0x0003, 0x0010, 0x0008 } };

    inline constexpr UartPins terminal{ 1, false, Pin(Port::B, 6), Pin(Port::B, 7) };
    inline constexpr uint32_t terminalBaudRate = 921600;
    inline constexpr DmaRequests terminalDma{ DMA_REQUEST_USART1_TX, DMA_REQUEST_USART1_RX };

    inline constexpr HilPinId debugLed = Pin(Port::B, 5);
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

    inline constexpr std::optional<UartPins> defaultUart = UartPins{ 1, true, Pin(Port::A, 2), Pin(Port::A, 3) };
    inline constexpr QeiPins defaultQei{ 2, Pin(Port::A, 15), Pin(Port::A, 1), Pin(Port::C, 6) };
    inline constexpr std::array<I2cPins, 2> i2cPins{ {
        { 1, Pin(Port::B, 8), Pin(Port::B, 9) },
        { 3, Pin(Port::C, 0), Pin(Port::C, 1) },
    } };
    inline constexpr QuadSpiPins qspiPins{ Pin(Port::A, 3), Pin(Port::A, 2), { { Pin(Port::B, 9), Pin(Port::B, 8), Pin(Port::A, 7), Pin(Port::A, 6) } } };
    inline constexpr LowPowerPins lowPowerDefaults{ Pin(Port::C, 6), Pin(Port::B, 0) };

    inline constexpr auto aliases = std::to_array<HilPinAlias>({
        { "terminaltx", terminal.tx },
        { "terminalrx", terminal.rx },
        { "ain1", Pin(Port::C, 0) },
        { "ain2", Pin(Port::C, 1) },
        { "ain3", Pin(Port::C, 2) },
        { "ain4", Pin(Port::C, 3) },
        { "ain5", Pin(Port::A, 0) },
        { "ain6", Pin(Port::A, 1) },
        { "tim1ch1", Pin(Port::A, 8) },
        { "tim1ch2", Pin(Port::A, 9) },
        { "tim1ch3", Pin(Port::A, 10) },
        { "tim1ch4", Pin(Port::A, 11) },
        { "tim1ch1n", Pin(Port::A, 7) },
        { "tim1ch2n", Pin(Port::B, 8) },
        { "tim1ch3n", Pin(Port::B, 9) },
        { "tim1bkin", Pin(Port::B, 12) },
        { "tim2ch1", Pin(Port::A, 15) },
        { "tim2ch2", Pin(Port::A, 1) },
        { "tim2ch3", Pin(Port::A, 2) },
        { "tim2ch4", Pin(Port::A, 3) },
        { "tim16ch1", Pin(Port::A, 6) },
        { "tim17ch1", Pin(Port::B, 9) },
        { "qei1a", Pin(Port::A, 8) },
        { "qei1b", Pin(Port::A, 9) },
        { "qei2a", defaultQei.a },
        { "qei2b", defaultQei.b },
        { "qei2idx", defaultQei.idx },
        { "lptim1in1", Pin(Port::C, 0) },
        { "lptim1in2", Pin(Port::C, 2) },
        { "spi1clk", Pin(Port::A, 5) },
        { "spi1miso", Pin(Port::A, 6) },
        { "spi1mosi", Pin(Port::A, 7) },
        { "spi1cs", Pin(Port::A, 4) },
        { "lpuart1tx", defaultUart->tx },
        { "lpuart1rx", defaultUart->rx },
        { "lpuart1rts", Pin(Port::B, 12) },
        { "lpuart1cts", Pin(Port::A, 6) },
        { "led0", Pin(Port::B, 0) },
        { "led1", Pin(Port::B, 1) },
        { "gpio0", Pin(Port::C, 6) },
        { "gpio1", Pin(Port::C, 10) },
        { "gpio2", Pin(Port::C, 12) },
        { "gpio3", Pin(Port::C, 13) },
        { "gpio4", Pin(Port::E, 4) },
        { "sw1", Pin(Port::C, 4) },
        { "sw2", Pin(Port::D, 0) },
        { "sw3", Pin(Port::D, 1) },
        { "i2c1scl", i2cPins[0].scl },
        { "i2c1sda", i2cPins[0].sda },
        { "i2c3scl", i2cPins[1].scl },
        { "i2c3sda", i2cPins[1].sda },
        { "spi2clk", Pin(Port::B, 13) },
        { "spi2miso", Pin(Port::B, 14) },
        { "spi2mosi", Pin(Port::B, 15) },
        { "spi2nss", Pin(Port::B, 12) },
        { "spi1nss", Pin(Port::A, 4) },
        { "qspiclk", qspiPins.clk },
        { "qspincs", qspiPins.ncs },
        { "qspiio0", qspiPins.io[0] },
        { "qspiio1", qspiPins.io[1] },
        { "qspiio2", qspiPins.io[2] },
        { "qspiio3", qspiPins.io[3] },
        { "mco", Pin(Port::A, 8) },
    });

    inline constexpr uint8_t uartDmaChannel = 3;
    inline constexpr uint8_t spiDmaChannel = 5;
    inline constexpr uint8_t adcDmaChannel = 7;
    inline constexpr DmaChannel adcDma{ 1, adcDmaChannel };
    inline constexpr DmaPair spiSlaveDma{ { 2, 1 }, { 2, 2 } };
    inline constexpr DmaChannel qspiDma{ 2, 3 };
    inline constexpr uint8_t qspiDmaRequest = DMA_REQUEST_QUADSPI;
    inline constexpr DmaChannel dmaGroupDma{ 2, 4 };
    inline constexpr uint8_t dmaGroupDmaRequest = DMA_REQUEST_TIM2_UP;

    inline constexpr uint8_t scaffoldTimer = 17;
    inline constexpr uint32_t flashScratchFirstPage = 64;
    inline constexpr uint32_t flashScratchEndPage = 128;

    constexpr std::optional<DmaRequests> UartDma(uint8_t index, bool lpuart)
    {
        if (index != 1)
            return std::nullopt;

        if (lpuart)
            return DmaRequests{ DMA_REQUEST_LPUART1_TX, DMA_REQUEST_LPUART1_RX };

        return DmaRequests{ DMA_REQUEST_USART1_TX, DMA_REQUEST_USART1_RX };
    }

    constexpr std::optional<DmaRequests> SpiDma(uint8_t index)
    {
        switch (index)
        {
            case 1:
                return DmaRequests{ DMA_REQUEST_SPI1_TX, DMA_REQUEST_SPI1_RX };
            case 2:
                return DmaRequests{ DMA_REQUEST_SPI2_TX, DMA_REQUEST_SPI2_RX };
            default:
                return std::nullopt;
        }
    }

    inline constexpr uint8_t adc = 1;
    inline constexpr uint8_t adcDmaRequest = DMA_REQUEST_ADC1;
    inline constexpr std::array<uint8_t, 2> adcTriggerTimers{ { 1, 2 } };
    inline constexpr uint32_t adcDefaultSamplingTime = ADC_SAMPLETIME_2CYCLES_5;
    inline constexpr std::array<services::HilChoice<uint32_t>, 8> adcSamplingTimes{ {
        { "2.5", ADC_SAMPLETIME_2CYCLES_5 },
        { "6.5", ADC_SAMPLETIME_6CYCLES_5 },
        { "12.5", ADC_SAMPLETIME_12CYCLES_5 },
        { "24.5", ADC_SAMPLETIME_24CYCLES_5 },
        { "47.5", ADC_SAMPLETIME_47CYCLES_5 },
        { "92.5", ADC_SAMPLETIME_92CYCLES_5 },
        { "247.5", ADC_SAMPLETIME_247CYCLES_5 },
        { "640.5", ADC_SAMPLETIME_640CYCLES_5 },
    } };

    inline uint32_t UartKernelClock(uint8_t, bool lpuart)
    {
        return HAL_RCCEx_GetPeriphCLKFreq(lpuart ? RCC_PERIPHCLK_LPUART1 : RCC_PERIPHCLK_USART1);
    }

    inline uint32_t SpiKernelClock(uint8_t index)
    {
        return index == 1 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
    }

    inline void InitializeClocks()
    {
        ConfigureDefaultClockNucleoWB55RG();
    }
}
