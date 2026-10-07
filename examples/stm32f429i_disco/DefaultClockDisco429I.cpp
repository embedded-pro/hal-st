#include DEVICE_HEADER
#include "examples/stm32f429i_disco/DefaultClockDisco429I.hpp"

/* The system Clock is configured as follows:
 *    System Clock source            = PLL (HSE)
 *    SYSCLK(Hz)                     = 180000000
 *    HCLK(Hz)                       = 180000000
 *    AHB Prescaler                  = 1
 *    APB1 Prescaler                 = 4
 *    APB2 Prescaler                 = 2
 *    HSE Frequency(Hz)              = HSE_VALUE
 *    VDD(V)                         = 3.3
 *    Main regulator output voltage  = Scale1 mode with over-drive
 *    Flash Latency(WS)              = 5
 *
 * The SDRAM timings of SdRamStm assume a 90 MHz SDRAM clock, which is half of HCLK
 */
void ConfigureDefaultClockDisco429I()
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {};
    RCC_OscInitTypeDef RCC_OscInitStruct = {};

    __PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

    // This assumes the HSE_VALUE is a multiple of 1MHz. PLLSAI shares this divider
    RCC_OscInitStruct.PLL.PLLM = (HSE_VALUE / 1000000u); // Divides HSE to 1 MHz
    RCC_OscInitStruct.PLL.PLLN = 360;                    // Multiplies the 1 MHz to 360 MHz
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;          // Divides the 360 MHz to 180 MHz for SYSCLK
    RCC_OscInitStruct.PLL.PLLQ = 7;                      // Divides the 360 MHz to 51 MHz for USB, SDIO, and RNG, which the example does not use
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    HAL_PWREx_EnableOverDrive();

    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
}

/* PLLSAI input    = HSE_VALUE / PLLM = 1 MHz
 * PLLSAI VCO      = 1 MHz * PLLSAIN = 192 MHz
 * PLLSAI R output = 192 MHz / PLLSAIR = 48 MHz
 * Pixel clock     = 48 MHz / 8 = 6 MHz
 */
void ConfigureLtdcClockDisco429I()
{
    RCC_PeriphCLKInitTypeDef peripheralClock = {};

    peripheralClock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    peripheralClock.PLLSAI.PLLSAIN = 192;
    peripheralClock.PLLSAI.PLLSAIR = 4;
    peripheralClock.PLLSAIDivR = RCC_PLLSAIDIVR_8;
    HAL_RCCEx_PeriphCLKConfig(&peripheralClock);
}
