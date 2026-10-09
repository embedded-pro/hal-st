#include DEVICE_HEADER
#include "demo/stm32h757i_eval/DefaultClockEvalH757I.hpp"

/* The system Clock is configured as follows (Cortex-M7; the Cortex-M4 runs from HCLK):
 *    System Clock source            = PLL (HSE)
 *    SYSCLK(Hz)                     = 400000000
 *    HCLK(Hz)                       = 200000000
 *    AHB Prescaler                  = 2
 *    APB1 Prescaler                 = 2
 *    APB2 Prescaler                 = 2
 *    APB3 Prescaler                 = 2
 *    APB4 Prescaler                 = 2
 *    HSE Frequency(Hz)              = 25MHz
 *    VDD(V)                         = 3.3
 *    Main regulator output voltage  = Scale1 mode
 */
void ConfigureDefaultClockEvalH757I()
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {};
    RCC_OscInitTypeDef RCC_OscInitStruct = {};

    // Configure the main internal regulator output voltage
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    // Enable HSE Oscillator and activate PLL with HSE as source
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
    RCC_OscInitStruct.CSIState = RCC_CSI_OFF;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 5;   // Divides the 25MHz HSE to 5MHz
    RCC_OscInitStruct.PLL.PLLN = 160; // Multiplies the 5MHz to 800MHz
    RCC_OscInitStruct.PLL.PLLP = 2;   // Divides the 800MHz to 400MHz for SYSCLK
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    // Select PLL as system clock source and configure the HCLK and the bus clocks dividers
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

void ConfigureLtdcClockEvalH757I()
{
    RCC_PeriphCLKInitTypeDef peripheralClock = {};

    peripheralClock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    peripheralClock.PLL3.PLL3M = 5;
    peripheralClock.PLL3.PLL3N = 132;
    peripheralClock.PLL3.PLL3P = 2;
    peripheralClock.PLL3.PLL3Q = 2;
    peripheralClock.PLL3.PLL3R = 24;
    peripheralClock.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
    peripheralClock.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    peripheralClock.PLL3.PLL3FRACN = 0;
    HAL_RCCEx_PeriphCLKConfig(&peripheralClock);
}
