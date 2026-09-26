#include "hal_st/middlewares/ble_middleware/LinkLayerPlatformWba.hpp"
#include "infra/event/EventDispatcher.hpp"
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
#include "power_table.h"
}

namespace
{
    constexpr IRQn_Type radioIrq = RADIO_IRQn;

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

    class InterruptsMasked
    {
    public:
        InterruptsMasked()
            : primask(__get_PRIMASK())
        {
            __disable_irq();
        }

        InterruptsMasked(const InterruptsMasked&) = delete;
        InterruptsMasked& operator=(const InterruptsMasked&) = delete;

        ~InterruptsMasked()
        {
            __set_PRIMASK(primask);
        }

    private:
        uint32_t primask;
    };

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
    LinkLayerPlatformWba::LinkLayerPlatformWba(RandomDataGeneratorCreator& randomDataGeneratorCreator, const Config& config)
        : config(config)
        , randomDataGeneratorCreator(randomDataGeneratorCreator)
    {
        LL_RCC_HSE_Enable();
        while (LL_RCC_HSE_IsReady() == 0)
        {
        }

        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        ConfigureSleepClock();
        ConfigureRandomDataGeneratorClock();
        RefillRandomDataPool();
    }

    int8_t LinkLayerPlatformWba::MaxTransmitPower() const
    {
        auto tables = infra::MemoryRange<const power_table_id_t>(ll_tx_power_tables, ll_tx_power_tables + num_of_supported_power_tables);
        auto table = std::find_if(tables.begin(), tables.end(), [this](const power_table_id_t& entry)
            {
                return entry.power_table_id == static_cast<uint8_t>(config.txPowerTable);
            });

        really_assert(table != tables.end() && table->tx_power_levels_count != 0);
        return table->ptr_tx_power_table[table->tx_power_levels_count - 1].tx_pwr;
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
        ConfigureSchedulerTimings();
    }

    void LinkLayerPlatformWba::ConfigureSchedulerTimings()
    {
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

    void LinkLayerPlatformWba::GenerateRandomData(infra::ByteRange result)
    {
        while (!result.empty())
        {
            if (randomDataPoolCount == 0 && __get_IPSR() == 0)
                RefillRandomDataPool();

            auto word = TakeRandomWord(result.size());
            auto bytes = infra::Head(infra::MakeByteRange(word), result.size());
            infra::Copy(bytes, infra::Head(result, bytes.size()));
            result = infra::DiscardHead(result, bytes.size());
        }

        ScheduleRandomDataPoolRefill();
    }

    void LinkLayerPlatformWba::SetupRadioInterrupt(void (*callback)())
    {
        radioCallback = callback;
        HAL_NVIC_SetPriority(radioIrq, radioActivePriority, 0);
        HAL_NVIC_EnableIRQ(radioIrq);
    }

    // The interrupt is pended and re-prioritised on the fly, which the handler does not offer, so that goes through HAL_NVIC
    void LinkLayerPlatformWba::SetupSoftwareLowInterrupt(void (*callback)())
    {
        softwareLowCallback = callback;

        if (!softwareLowInterrupt)
            softwareLowInterrupt.emplace(config.softwareLowInterrupt, [this]()
                {
                    SoftwareLowInterrupt();
                });

        HAL_NVIC_SetPriority(SoftwareLowIrq(), softwareLowPriority, 0);
        HAL_NVIC_EnableIRQ(SoftwareLowIrq());
    }

    void LinkLayerPlatformWba::TriggerSoftwareLowInterrupt(uint8_t priority)
    {
        if (HAL_NVIC_GetActive(SoftwareLowIrq()) == 0)
            HAL_NVIC_SetPriority(SoftwareLowIrq(), priority == 0 ? softwareLowPriority : radioLowPriority, 0);
        else if (priority != 0)
            softwareLowPendingAtRadioLowPriority = true;

        HAL_NVIC_SetPendingIRQ(SoftwareLowIrq());
    }

    void LinkLayerPlatformWba::EnableInterrupts()
    {
        if (interruptsDisabledCount > 0 && --interruptsDisabledCount == 0)
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
            HAL_NVIC_EnableIRQ(SoftwareLowIrq());

        if ((isrType & SYS_LOW_ISR) != 0 && --systemLowInterruptsDisabledCount == 0)
            __set_BASEPRI(savedBasepri);
    }

    void LinkLayerPlatformWba::DisableSpecificInterrupts(uint8_t isrType)
    {
        if ((isrType & LL_HIGH_ISR_ONLY) != 0 && ++radioInterruptDisabledCount == 1)
            HAL_NVIC_DisableIRQ(radioIrq);

        if ((isrType & LL_LOW_ISR_ONLY) != 0 && ++softwareLowInterruptDisabledCount == 1)
            HAL_NVIC_DisableIRQ(SoftwareLowIrq());

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
        HAL_NVIC_DisableIRQ(SoftwareLowIrq());

        if (softwareLowCallback != nullptr)
            softwareLowCallback();

        if (softwareLowPendingAtRadioLowPriority.exchange(false))
            HAL_NVIC_SetPriority(SoftwareLowIrq(), radioLowPriority, 0);

        HAL_NVIC_EnableIRQ(SoftwareLowIrq());
    }

    void LinkLayerPlatformWba::ConfigureSleepClock() const
    {
        StartSleepClockOscillator();

        RCC_PeriphCLKInitTypeDef sleepTimerClock{};
        sleepTimerClock.PeriphClockSelection = RCC_PERIPHCLK_RADIOST;
        sleepTimerClock.RadioSlpTimClockSelection = RadioSleepTimerClockSelection(config.sleepClockSource);
        really_assert(HAL_RCCEx_PeriphCLKConfig(&sleepTimerClock) == HAL_OK);
    }

    // HSE, divided by 1000, is already running for the radio
    void LinkLayerPlatformWba::StartSleepClockOscillator() const
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

    IRQn_Type LinkLayerPlatformWba::SoftwareLowIrq() const
    {
        return static_cast<IRQn_Type>(config.softwareLowInterrupt);
    }

    void LinkLayerPlatformWba::RefillRandomDataPool()
    {
        std::size_t missing = 0;
        {
            InterruptsMasked masked;
            missing = randomDataPoolSize - randomDataPoolCount;
        }

        if (missing == 0)
            return;

        std::array<uint32_t, randomDataPoolSize> words;
        auto generated = infra::Head(infra::MakeRange(words), missing);

        {
            infra::ProxyCreator<SynchronousRandomDataGenerator, void()> generator(randomDataGeneratorCreator);
            generator->GenerateRandomData(infra::ReinterpretCastByteRange(generated));
        }

        AddToRandomDataPool(generated);
    }

    void LinkLayerPlatformWba::AddToRandomDataPool(infra::MemoryRange<const uint32_t> words)
    {
        InterruptsMasked masked;

        while (!words.empty() && randomDataPoolCount != randomDataPoolSize)
        {
            randomDataPool[randomDataPoolCount++] = words.back();
            words.pop_back();
        }
    }

    // As ST's HW_RNG_Get, an interrupt that finds the pool empty reuses a stale word rather than wait for the generator
    uint32_t LinkLayerPlatformWba::TakeRandomWord(std::size_t fallbackIndex)
    {
        InterruptsMasked masked;

        if (randomDataPoolCount != 0)
            return randomDataPool[--randomDataPoolCount];

        return ~randomDataPool[fallbackIndex % randomDataPoolSize];
    }

    void LinkLayerPlatformWba::ScheduleRandomDataPoolRefill()
    {
        if (!randomDataPoolRefillScheduled.exchange(true))
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    randomDataPoolRefillScheduled = false;
                    RefillRandomDataPool();
                });
    }
}

extern "C"
{
    // The radio interrupt runs at the highest priority, so it is taken directly rather than through the interrupt table
    void RADIO_IRQHandler()
    {
        hal::LinkLayerPlatformWba::Instance().RadioInterrupt();
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
