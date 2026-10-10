#include DEVICE_HEADER
#include "demo/stm32h745i_disco/DefaultClockDiscoveryH745I.hpp"
#include "infra/util/ReallyAssert.hpp"

void ConfigureDefaultClockDiscoveryH745I()
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {};
    RCC_OscInitTypeDef RCC_OscInitStruct = {};

    really_assert(HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY) == HAL_OK);

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
    RCC_OscInitStruct.CSIState = RCC_CSI_OFF;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 5;
    RCC_OscInitStruct.PLL.PLLN = 160;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    really_assert(HAL_RCC_OscConfig(&RCC_OscInitStruct) == HAL_OK);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
    really_assert(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) == HAL_OK);
}

void ConfigureLtdcClockDiscoveryH745I()
{
    RCC_PeriphCLKInitTypeDef peripheralClock = {};

    peripheralClock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    peripheralClock.PLL3.PLL3M = 5;
    peripheralClock.PLL3.PLL3N = 160;
    peripheralClock.PLL3.PLL3P = 2;
    peripheralClock.PLL3.PLL3Q = 2;
    peripheralClock.PLL3.PLL3R = 83;
    peripheralClock.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
    peripheralClock.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    peripheralClock.PLL3.PLL3FRACN = 0;
    really_assert(HAL_RCCEx_PeriphCLKConfig(&peripheralClock) == HAL_OK);
}

void ConfigureAudioClockDiscoveryH745I()
{
    RCC_PeriphCLKInitTypeDef peripheralClock = {};

    peripheralClock.PeriphClockSelection = RCC_PERIPHCLK_SAI23;
    peripheralClock.Sai23ClockSelection = RCC_SAI23CLKSOURCE_PLL2;
    peripheralClock.PLL2.PLL2M = 25;
    peripheralClock.PLL2.PLL2N = 344;
    peripheralClock.PLL2.PLL2P = 7;
    peripheralClock.PLL2.PLL2Q = 2;
    peripheralClock.PLL2.PLL2R = 2;
    peripheralClock.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_0;
    peripheralClock.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    peripheralClock.PLL2.PLL2FRACN = 524;
    really_assert(HAL_RCCEx_PeriphCLKConfig(&peripheralClock) == HAL_OK);
}
