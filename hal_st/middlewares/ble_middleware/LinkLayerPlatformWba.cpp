#include "hal_st/middlewares/ble_middleware/LinkLayerPlatformWba.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include DEVICE_HEADER
#include "stm32wbaxx_ll_pwr.h"
#include "stm32wbaxx_ll_rcc.h"
#include "stm32wbaxx_ll_system.h"

extern "C"
{
#include "common_types.h"
#include "linklayer_plat.h"
#include "ll_intf.h"
#include "ll_intf_cmn.h"
#include "ll_sys.h"
}

namespace
{
    constexpr IRQn_Type radioIrq = RADIO_IRQn;
    constexpr IRQn_Type softwareLowIrq = HASH_IRQn;

    constexpr uint32_t radioActivePriority = 0;
    constexpr uint32_t radioLowPriority = 5;
    constexpr uint32_t softwareLowPriority = 15;

    constexpr uint8_t useRadioLowIsr = 1;
    constexpr uint8_t nextEventSchedulingFromIsr = 1;

    constexpr uint8_t defaultSleepClockAccuracy = 0;

    constexpr uint8_t lsiCalibrationDurationSleepTimerCycles = 24;
    constexpr uint32_t lsiCalibrationPeriodMs = 15000;

    constexpr uint8_t defaultDriftTime = 13;
    constexpr uint8_t defaultExecutionTime = 10;
    constexpr uint8_t lsiExtraDriftTime = 9;
    constexpr uint8_t lsiExtraExecutionTime = 3;
    constexpr uint8_t debugBuildExtraDriftTime = 6;
    constexpr uint8_t debugBuildExtraExecutionTime = 4;

    uint32_t RadioSleepTimerClockSelection(hal::LinkLayerPlatformWba::SleepClockSource source)
    {
        switch (source)
        {
            case hal::LinkLayerPlatformWba::SleepClockSource::lse:
                return RCC_RADIOSTCLKSOURCE_LSE;
            case hal::LinkLayerPlatformWba::SleepClockSource::lsi:
                return RCC_RADIOSTCLKSOURCE_LSI;
            default:
                return RCC_RADIOSTCLKSOURCE_HSE_DIV1000;
        }
    }

    uint8_t LinkLayerSleepClockSource(hal::LinkLayerPlatformWba::SleepClockSource source)
    {
        switch (source)
        {
            case hal::LinkLayerPlatformWba::SleepClockSource::lse:
                return RTC_SLPTMR;
            case hal::LinkLayerPlatformWba::SleepClockSource::lsi:
                return RCO_SLPTMR;
            default:
                return CRYSTAL_OSCILLATOR_SLPTMR;
        }
    }
}

namespace hal
{
    LinkLayerPlatformWba::LinkLayerPlatformWba(const Config& config)
        : config(config)
    {
        LL_RCC_HSE_Enable();
        while (LL_RCC_HSE_IsReady() == 0)
        {
        }

        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        ConfigureSleepClock();
        ConfigureRandomDataGeneratorClock();
        randomDataGenerator.emplace();
    }

    void LinkLayerPlatformWba::ConfigureParameters()
    {
        ll_intf_cmn_config_ll_ctx_params(useRadioLowIsr, nextEventSchedulingFromIsr);
        SelectLinkLayerSleepClock();
        ll_intf_cmn_select_tx_power_table(static_cast<uint8_t>(config.txPowerTable));
    }

    void LinkLayerPlatformWba::Reset()
    {
        SelectLinkLayerSleepClock();
        ll_intf_le_set_sleep_clock_accuracy(SleepClockAccuracy());

        auto driftTime = defaultDriftTime;
        auto executionTime = defaultExecutionTime;

        if (config.sleepClockSource == SleepClockSource::lsi)
        {
            ll_intf_le_set_rco_clbr_evnt_params(lsiCalibrationDurationSleepTimerCycles, lsiCalibrationPeriodMs);
            driftTime += lsiExtraDriftTime;
            executionTime += lsiExtraExecutionTime;
        }
#ifndef NDEBUG
        else
        {
            driftTime += debugBuildExtraDriftTime;
            executionTime += debugBuildExtraExecutionTime;
        }
#endif

        if (driftTime != defaultDriftTime || executionTime != defaultExecutionTime)
            ll_sys_config_BLE_schldr_timings(driftTime, executionTime);
    }

    // Both the link layer, from its interrupts, and the host stack draw from the one generator; taking a word at a
    // time keeps the radio interrupt's latency to that of a single draw.
    void LinkLayerPlatformWba::GenerateRandomData(infra::ByteRange result)
    {
        while (!result.empty())
        {
            auto word = infra::Head(result, sizeof(uint32_t));

            auto primask = __get_PRIMASK();
            __disable_irq();
            randomDataGenerator->GenerateRandomData(word);
            __set_PRIMASK(primask);

            result = infra::DiscardHead(result, word.size());
        }
    }

    void LinkLayerPlatformWba::SetupRadioInterrupt(void (*callback)())
    {
        radioCallback = callback;
        HAL_NVIC_SetPriority(radioIrq, radioActivePriority, 0);
        HAL_NVIC_EnableIRQ(radioIrq);
    }

    void LinkLayerPlatformWba::SetupSoftwareLowInterrupt(void (*callback)())
    {
        softwareLowCallback = callback;
        HAL_NVIC_SetPriority(softwareLowIrq, softwareLowPriority, 0);
        HAL_NVIC_EnableIRQ(softwareLowIrq);
    }

    void LinkLayerPlatformWba::TriggerSoftwareLowInterrupt(uint8_t priority)
    {
        if (HAL_NVIC_GetActive(softwareLowIrq) == 0)
            HAL_NVIC_SetPriority(softwareLowIrq, priority == 0 ? softwareLowPriority : radioLowPriority, 0);
        else if (priority != 0)
            softwareLowPendingAtRadioLowPriority = true;

        HAL_NVIC_SetPendingIRQ(softwareLowIrq);
    }

    void LinkLayerPlatformWba::EnableInterrupts()
    {
        interruptsDisabledCount = std::max<int32_t>(0, interruptsDisabledCount - 1);

        if (interruptsDisabledCount == 0)
            __set_PRIMASK(savedPrimask);
    }

    void LinkLayerPlatformWba::DisableInterrupts()
    {
        if (interruptsDisabledCount == 0)
            savedPrimask = __get_PRIMASK();

        __disable_irq();
        ++interruptsDisabledCount;
    }

    void LinkLayerPlatformWba::EnableSpecificInterrupts(uint8_t isrType)
    {
        if ((isrType & LL_HIGH_ISR_ONLY) != 0 && --radioInterruptDisabledCount == 0)
            HAL_NVIC_EnableIRQ(radioIrq);

        if ((isrType & LL_LOW_ISR_ONLY) != 0 && --softwareLowInterruptDisabledCount == 0)
            HAL_NVIC_EnableIRQ(softwareLowIrq);

        if ((isrType & SYS_LOW_ISR) != 0 && --systemLowInterruptsDisabledCount == 0)
            __set_BASEPRI(savedBasepri);
    }

    void LinkLayerPlatformWba::DisableSpecificInterrupts(uint8_t isrType)
    {
        if ((isrType & LL_HIGH_ISR_ONLY) != 0 && ++radioInterruptDisabledCount == 1)
            HAL_NVIC_DisableIRQ(radioIrq);

        if ((isrType & LL_LOW_ISR_ONLY) != 0 && ++softwareLowInterruptDisabledCount == 1)
            HAL_NVIC_DisableIRQ(softwareLowIrq);

        if ((isrType & SYS_LOW_ISR) != 0 && ++systemLowInterruptsDisabledCount == 1)
        {
            savedBasepri = __get_BASEPRI();
            __set_BASEPRI_MAX(radioLowPriority << (8U - __NVIC_PRIO_BITS));
        }
    }

    void LinkLayerPlatformWba::NotifyWfiEnter()
    {
        if (LL_PWR_GetRadioMode() != LL_PWR_RADIO_ACTIVE_MODE || (__HAL_RCC_RADIO_IS_CLK_SLEEP_ENABLED() == 0 && LL_RCC_RADIO_IsEnabledSleepTimerClock() == 0))
            radioBusClockSwitchedOff = true;
    }

    void LinkLayerPlatformWba::NotifyWfiExit()
    {
        if (radioBusClockSwitchedOff)
            sleepTimerAtWfiExit = ll_intf_cmn_get_slptmr_value();
    }

    void LinkLayerPlatformWba::WaitRadioBusClockReady()
    {
        if (radioBusClockSwitchedOff)
        {
            radioBusClockSwitchedOff = false;
            while (sleepTimerAtWfiExit == ll_intf_cmn_get_slptmr_value())
            {
            }
        }
    }

    void LinkLayerPlatformWba::RadioInterrupt()
    {
        if (radioCallback != nullptr)
            radioCallback();

        LL_RCC_RADIO_DisableSleepTimerClock();
        __ISB();
    }

    void LinkLayerPlatformWba::SoftwareLowInterrupt()
    {
        HAL_NVIC_DisableIRQ(softwareLowIrq);

        if (softwareLowCallback != nullptr)
            softwareLowCallback();

        if (softwareLowPendingAtRadioLowPriority.exchange(false))
            HAL_NVIC_SetPriority(softwareLowIrq, radioLowPriority, 0);

        HAL_NVIC_EnableIRQ(softwareLowIrq);
    }

    void LinkLayerPlatformWba::ConfigureSleepClock() const
    {
        RCC_OscInitTypeDef oscillator{};

        if (config.sleepClockSource == SleepClockSource::lse)
        {
            HAL_PWR_EnableBkUpAccess();
            __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMLOW);
            oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
            oscillator.LSEState = RCC_LSE_ON;
            really_assert(HAL_RCC_OscConfig(&oscillator) == HAL_OK);
        }
        else if (config.sleepClockSource == SleepClockSource::lsi)
        {
            oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSI;
            oscillator.LSIState = RCC_LSI1_ON;
            oscillator.LSIDiv = RCC_LSI_DIV1;
            really_assert(HAL_RCC_OscConfig(&oscillator) == HAL_OK);
        }

        RCC_PeriphCLKInitTypeDef sleepTimerClock{};
        sleepTimerClock.PeriphClockSelection = RCC_PERIPHCLK_RADIOST;
        sleepTimerClock.RadioSlpTimClockSelection = RadioSleepTimerClockSelection(config.sleepClockSource);
        really_assert(HAL_RCCEx_PeriphCLKConfig(&sleepTimerClock) == HAL_OK);
    }

    void LinkLayerPlatformWba::ConfigureRandomDataGeneratorClock() const
    {
        LL_RCC_HSI_Enable();
        while (LL_RCC_HSI_IsReady() == 0)
        {
        }

        RCC_PeriphCLKInitTypeDef randomDataGeneratorClock{};
        randomDataGeneratorClock.PeriphClockSelection = RCC_PERIPHCLK_RNG;
        randomDataGeneratorClock.RngClockSelection = RCC_RNGCLKSOURCE_HSI;
        really_assert(HAL_RCCEx_PeriphCLKConfig(&randomDataGeneratorClock) == HAL_OK);
    }

    void LinkLayerPlatformWba::SelectLinkLayerSleepClock() const
    {
        uint16_t frequency = 0;
        ll_intf_cmn_le_select_slp_clk_src(LinkLayerSleepClockSource(config.sleepClockSource), &frequency);
    }

    uint8_t LinkLayerPlatformWba::SleepClockAccuracy() const
    {
        if (config.sleepClockSource == SleepClockSource::lse)
            return config.lseSleepClockAccuracy;

        return defaultSleepClockAccuracy;
    }
}

// The link layer masks, pends and re-prioritises its interrupts on the fly, which InterruptHandler does not
// offer, so its vectors are overridden directly and driven through HAL_NVIC, as in ST's reference application.
extern "C"
{
    void RADIO_IRQHandler()
    {
        hal::LinkLayerPlatformWba::Instance().RadioInterrupt();
    }

    void HASH_IRQHandler()
    {
        hal::LinkLayerPlatformWba::Instance().SoftwareLowInterrupt();
    }

    void LINKLAYER_PLAT_ClockInit()
    {
        really_assert(LL_RCC_RADIO_GetSleepTimerClockSource() != LL_RCC_RADIOSLEEPSOURCE_NONE);
        __HAL_RCC_RADIO_CLK_ENABLE();
    }

    void LINKLAYER_PLAT_DelayUs(uint32_t delay)
    {
        auto start = DWT->CYCCNT;
        auto cycles = delay * (SystemCoreClock / 1000000U);

        while (DWT->CYCCNT - start < cycles)
        {
        }
    }

    void LINKLAYER_PLAT_Assert(uint8_t condition)
    {
        really_assert(condition != 0);
    }

    void LINKLAYER_PLAT_AclkCtrl(uint8_t enable)
    {
        if (enable != 0)
        {
            HAL_RCCEx_EnableRadioBBClock();
            while (LL_RCC_HSE_IsReady() == 0)
            {
            }
        }
        else
            HAL_RCCEx_DisableRadioBBClock();
    }

    void LINKLAYER_PLAT_NotifyWFIEnter()
    {
        hal::LinkLayerPlatformWba::Instance().NotifyWfiEnter();
    }

    void LINKLAYER_PLAT_NotifyWFIExit()
    {
        hal::LinkLayerPlatformWba::Instance().NotifyWfiExit();
    }

    void LINKLAYER_PLAT_WaitHclkRdy()
    {
        hal::LinkLayerPlatformWba::Instance().WaitRadioBusClockReady();
    }

    void LINKLAYER_PLAT_GetRNG(uint8_t* ptr_rnd, uint32_t len)
    {
        hal::LinkLayerPlatformWba::Instance().GenerateRandomData(infra::ByteRange(ptr_rnd, ptr_rnd + len));
    }

    void LINKLAYER_PLAT_SetupRadioIT(void (*intr_cb)())
    {
        hal::LinkLayerPlatformWba::Instance().SetupRadioInterrupt(intr_cb);
    }

    void LINKLAYER_PLAT_SetupSwLowIT(void (*intr_cb)())
    {
        hal::LinkLayerPlatformWba::Instance().SetupSoftwareLowInterrupt(intr_cb);
    }

    void LINKLAYER_PLAT_TriggerSwLowIT(uint8_t priority)
    {
        hal::LinkLayerPlatformWba::Instance().TriggerSoftwareLowInterrupt(priority);
    }

    void LINKLAYER_PLAT_EnableIRQ()
    {
        hal::LinkLayerPlatformWba::Instance().EnableInterrupts();
    }

    void LINKLAYER_PLAT_DisableIRQ()
    {
        hal::LinkLayerPlatformWba::Instance().DisableInterrupts();
    }

    void LINKLAYER_PLAT_EnableSpecificIRQ(uint8_t isr_type)
    {
        hal::LinkLayerPlatformWba::Instance().EnableSpecificInterrupts(isr_type);
    }

    void LINKLAYER_PLAT_DisableSpecificIRQ(uint8_t isr_type)
    {
        hal::LinkLayerPlatformWba::Instance().DisableSpecificInterrupts(isr_type);
    }

    void LINKLAYER_PLAT_EnableRadioIT()
    {
        HAL_NVIC_EnableIRQ(radioIrq);
    }

    void LINKLAYER_PLAT_DisableRadioIT()
    {
        HAL_NVIC_DisableIRQ(radioIrq);
    }

    void LINKLAYER_PLAT_StartRadioEvt()
    {
        __HAL_RCC_RADIO_CLK_SLEEP_ENABLE();
        HAL_NVIC_SetPriority(radioIrq, radioActivePriority, 0);
    }

    void LINKLAYER_PLAT_StopRadioEvt()
    {
        __HAL_RCC_RADIO_CLK_SLEEP_DISABLE();
        HAL_NVIC_SetPriority(radioIrq, radioLowPriority, 0);
    }

    // hal-st has neither ST's low power manager nor its system clock manager, so there is nothing to hold off
    // or switch around a calibration, and the link layer calibrates periodically instead of on temperature.
    void LINKLAYER_PLAT_RCOStartClbr()
    {}

    void LINKLAYER_PLAT_RCOStopClbr()
    {}

    void LINKLAYER_PLAT_RequestTemperature()
    {}

    void LINKLAYER_PLAT_EnableOSContextSwitch()
    {}

    void LINKLAYER_PLAT_DisableOSContextSwitch()
    {}

    void LINKLAYER_PLAT_SCHLDR_TIMING_UPDATE_NOT(Evnt_timing_t*)
    {}

    uint32_t LINKLAYER_PLAT_GetSTCompanyID()
    {
        return LL_FLASH_GetSTCompanyID();
    }

    uint32_t LINKLAYER_PLAT_GetUDN()
    {
        return LL_FLASH_GetUDN();
    }

    // ST declares these with an enum of debug signals from its RTDebug module, which hal-st does not ship.
    void LINKLAYER_DEBUG_SIGNAL_SET(int)
    {}

    void LINKLAYER_DEBUG_SIGNAL_RESET(int)
    {}

    void LINKLAYER_DEBUG_SIGNAL_TOGGLE(int)
    {}

    void ll_sys_reset()
    {
        hal::LinkLayerPlatformWba::Instance().Reset();
    }

    void ll_sys_config_params()
    {
        hal::LinkLayerPlatformWba::Instance().ConfigureParameters();
    }
}
