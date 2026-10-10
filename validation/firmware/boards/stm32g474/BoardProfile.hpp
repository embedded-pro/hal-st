#pragma once

#include "hal_st/stm32fxxx/DefaultClockNucleoG474xxx.hpp"
#include "services/hil/HilArguments.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

namespace validation::board
{
    using hal::Port;

    inline constexpr const char* name = "NUCLEO-G474RE";
    inline constexpr const char* family = "stm32g474";

    inline constexpr unsigned int hseValue = 24'000'000;

    inline constexpr const char* portLetters = "ABCDEFG";
    inline constexpr uint8_t maximumPinIndex = 15;
    static_assert(static_cast<uint8_t>(Port::G) == 6, "portLetters must follow hal::Port");

    // The generated pinout table lists the pins of the die; only these are bonded out on the LQFP64: PA-PC, PD2, PF0, PF1 and PG10
    inline constexpr std::array<uint16_t, 7> bondedPins{ { 0xffff, 0xffff, 0xffff, 0x0004, 0x0000, 0x0003, 0x0400 } };

    inline constexpr UartPins terminal{ 2, false, Pin(Port::A, 2), Pin(Port::A, 3) };
    inline constexpr uint32_t terminalBaudRate = 921600;
    inline constexpr DmaRequests terminalDma{ DMA_REQUEST_USART2_TX, DMA_REQUEST_USART2_RX };

    inline constexpr HilPinId debugLed = Pin(Port::A, 5);
    inline constexpr std::array<HilPinId, 10> reservedPins{ {
        terminal.tx,
        terminal.rx,
        debugLed,
        Pin(Port::A, 13),
        Pin(Port::A, 14),
        Pin(Port::C, 14),
        Pin(Port::C, 15),
        Pin(Port::F, 0),
        Pin(Port::F, 1),
        Pin(Port::G, 10),
    } };

    inline constexpr std::optional<UartPins> defaultUart = UartPins{ 1, true, Pin(Port::C, 1), Pin(Port::C, 0) };
    inline constexpr QeiPins defaultQei{ 3, Pin(Port::C, 6), Pin(Port::C, 7), Pin(Port::C, 8) };
    inline constexpr std::array<I2cPins, 2> i2cPins{ {
        { 1, Pin(Port::B, 8), Pin(Port::B, 9) },
        { 3, Pin(Port::C, 8), Pin(Port::C, 9) },
    } };
    inline constexpr QuadSpiPins qspiPins{ Pin(Port::B, 10), Pin(Port::B, 11), { { Pin(Port::B, 1), Pin(Port::B, 0), Pin(Port::A, 7), Pin(Port::A, 6) } } };
    inline constexpr std::array<DacOutput, 2> dacOutputs{ {
        { 1, Pin(Port::A, 4) },
        { 2, Pin(Port::A, 6) },
    } };
    inline constexpr LowPowerPins lowPowerDefaults{ Pin(Port::C, 4), Pin(Port::C, 5) };

    inline constexpr auto aliases = std::to_array<HilPinAlias>({
        { "terminaltx", terminal.tx },
        { "terminalrx", terminal.rx },
        { "ain1", Pin(Port::A, 0) },
        { "ain2", Pin(Port::A, 1) },
        { "ain3", Pin(Port::B, 0) },
        { "ain4", Pin(Port::C, 1) },
        { "ain5", Pin(Port::C, 0) },
        { "ain6", Pin(Port::C, 2) },
        { "ain7", Pin(Port::C, 3) },
        { "ain8", Pin(Port::B, 1) },
        { "tim1ch1", Pin(Port::A, 8) },
        { "tim1ch2", Pin(Port::A, 9) },
        { "tim1ch3", Pin(Port::A, 10) },
        { "tim1ch4", Pin(Port::A, 11) },
        { "tim1ch1n", Pin(Port::A, 7) },
        { "tim1ch2n", Pin(Port::B, 0) },
        { "tim1ch3n", Pin(Port::B, 1) },
        { "tim1bkin", Pin(Port::B, 12) },
        { "tim2ch1", Pin(Port::A, 0) },
        { "tim2ch2", Pin(Port::A, 1) },
        { "tim2ch3", Pin(Port::B, 10) },
        { "tim2ch4", Pin(Port::B, 11) },
        { "tim3ch1", Pin(Port::C, 6) },
        { "tim3ch2", Pin(Port::C, 7) },
        { "tim3ch3", Pin(Port::C, 8) },
        { "tim3ch4", Pin(Port::C, 9) },
        { "tim4ch1", Pin(Port::B, 6) },
        { "tim4ch2", Pin(Port::B, 7) },
        { "tim4ch3", Pin(Port::B, 8) },
        { "tim4ch4", Pin(Port::B, 9) },
        { "tim8ch1", Pin(Port::C, 6) },
        { "tim8ch2", Pin(Port::C, 7) },
        { "tim8ch3", Pin(Port::C, 8) },
        { "tim8ch4", Pin(Port::C, 9) },
        { "tim15ch1", Pin(Port::B, 14) },
        { "tim15ch2", Pin(Port::B, 15) },
        { "tim16ch1", Pin(Port::A, 6) },
        { "tim16ch1n", Pin(Port::B, 6) },
        { "tim17ch1", Pin(Port::B, 9) },
        { "tim17ch1n", Pin(Port::B, 7) },
        { "tim20ch1", Pin(Port::B, 2) },
        { "tim20ch2", Pin(Port::C, 2) },
        { "qei1a", Pin(Port::A, 8) },
        { "qei1b", Pin(Port::A, 9) },
        { "qei1idx", Pin(Port::A, 10) },
        { "qei2a", Pin(Port::A, 0) },
        { "qei2b", Pin(Port::A, 1) },
        { "qei2idx", Pin(Port::A, 15) },
        { "qei3a", defaultQei.a },
        { "qei3b", defaultQei.b },
        { "qei3idx", defaultQei.idx },
        { "qei4a", Pin(Port::B, 6) },
        { "qei4b", Pin(Port::B, 7) },
        { "qei4idx", Pin(Port::B, 8) },
        { "lptim1in1", Pin(Port::B, 5) },
        { "lptim1in2", Pin(Port::B, 7) },
        { "lptim1ch1", Pin(Port::B, 2) },
        { "spi1clk", Pin(Port::B, 3) },
        { "spi1miso", Pin(Port::B, 4) },
        { "spi1mosi", Pin(Port::B, 5) },
        { "spi1cs", Pin(Port::A, 15) },
        { "spi1nss", Pin(Port::A, 15) },
        { "spi2clk", Pin(Port::B, 13) },
        { "spi2miso", Pin(Port::B, 14) },
        { "spi2mosi", Pin(Port::B, 15) },
        { "spi2cs", Pin(Port::B, 12) },
        { "spi2nss", Pin(Port::B, 12) },
        { "spi3clk", Pin(Port::C, 10) },
        { "spi3miso", Pin(Port::C, 11) },
        { "spi3mosi", Pin(Port::C, 12) },
        { "spi3cs", Pin(Port::A, 4) },
        { "spi3nss", Pin(Port::A, 4) },
        { "i2c1scl", i2cPins[0].scl },
        { "i2c1sda", i2cPins[0].sda },
        { "i2c2scl", Pin(Port::A, 9) },
        { "i2c2sda", Pin(Port::A, 8) },
        { "i2c3scl", i2cPins[1].scl },
        { "i2c3sda", i2cPins[1].sda },
        { "i2c4scl", Pin(Port::C, 6) },
        { "i2c4sda", Pin(Port::C, 7) },
        { "usart1tx", Pin(Port::A, 9) },
        { "usart1rx", Pin(Port::A, 10) },
        { "usart1rts", Pin(Port::A, 12) },
        { "usart1cts", Pin(Port::A, 11) },
        { "usart3tx", Pin(Port::B, 10) },
        { "usart3rx", Pin(Port::B, 11) },
        { "usart3rts", Pin(Port::B, 14) },
        { "usart3cts", Pin(Port::B, 13) },
        { "uart4tx", Pin(Port::C, 10) },
        { "uart4rx", Pin(Port::C, 11) },
        { "uart5tx", Pin(Port::C, 12) },
        { "uart5rx", Pin(Port::D, 2) },
        { "lpuart1tx", defaultUart->tx },
        { "lpuart1rx", defaultUart->rx },
        { "lpuart1rts", Pin(Port::B, 12) },
        { "lpuart1cts", Pin(Port::B, 13) },
        { "dac1out1", dacOutputs[0].pin },
        { "dac2out1", dacOutputs[1].pin },
        { "gpio0", Pin(Port::C, 4) },
        { "gpio1", Pin(Port::C, 5) },
        { "gpio2", Pin(Port::D, 2) },
        { "gpio3", Pin(Port::B, 2) },
        { "gpio4", Pin(Port::A, 4) },
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
    inline constexpr uint32_t flashScratchFirstPage = 176;
    inline constexpr uint32_t flashScratchEndPage = 252;

    constexpr std::optional<DmaRequests> UartDma(uint8_t index, bool lpuart)
    {
        if (lpuart)
            return index == 1 ? std::make_optional(DmaRequests{ DMA_REQUEST_LPUART1_TX, DMA_REQUEST_LPUART1_RX }) : std::nullopt;

        switch (index)
        {
            case 1:
                return DmaRequests{ DMA_REQUEST_USART1_TX, DMA_REQUEST_USART1_RX };
            case 2:
                return DmaRequests{ DMA_REQUEST_USART2_TX, DMA_REQUEST_USART2_RX };
            case 3:
                return DmaRequests{ DMA_REQUEST_USART3_TX, DMA_REQUEST_USART3_RX };
            case 4:
                return DmaRequests{ DMA_REQUEST_UART4_TX, DMA_REQUEST_UART4_RX };
            case 5:
                return DmaRequests{ DMA_REQUEST_UART5_TX, DMA_REQUEST_UART5_RX };
            default:
                return std::nullopt;
        }
    }

    constexpr std::optional<DmaRequests> SpiDma(uint8_t index)
    {
        switch (index)
        {
            case 1:
                return DmaRequests{ DMA_REQUEST_SPI1_TX, DMA_REQUEST_SPI1_RX };
            case 2:
                return DmaRequests{ DMA_REQUEST_SPI2_TX, DMA_REQUEST_SPI2_RX };
            case 3:
                return DmaRequests{ DMA_REQUEST_SPI3_TX, DMA_REQUEST_SPI3_RX };
            default:
                return std::nullopt;
        }
    }

    inline constexpr uint8_t adc = 1;
    inline constexpr uint8_t adcDmaRequest = DMA_REQUEST_ADC1;
    inline constexpr std::array<uint8_t, 2> adcTriggerTimers{ { 1, 2 } };
    inline constexpr std::array<uint8_t, 3> breakFilterTimers{ { 1, 8, 20 } };
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

    inline uint32_t UartKernelClock(uint8_t index, bool lpuart)
    {
        if (lpuart)
            return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_LPUART1);

        switch (index)
        {
            case 1:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART1);
            case 2:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART2);
            case 3:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART3);
            case 4:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_UART4);
            default:
                return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_UART5);
        }
    }

    inline uint32_t SpiKernelClock(uint8_t index)
    {
        return index == 1 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
    }

    inline void InitializeClocks()
    {
        ConfigureDefaultClockNucleoG474xxx();

        // CLK48 comes from HSI48 out of reset and the RNG has no other kernel clock
        RCC_OscInitTypeDef oscillators{};
        oscillators.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
        oscillators.HSI48State = RCC_HSI48_ON;
        oscillators.PLL.PLLState = RCC_PLL_NONE;
        HAL_RCC_OscConfig(&oscillators);
    }
}
