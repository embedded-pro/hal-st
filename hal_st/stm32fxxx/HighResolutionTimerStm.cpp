#include "hal_st/stm32fxxx/HighResolutionTimerStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>

#if defined(HAS_PERIPHERAL_HRTIM)

namespace hal
{
    namespace
    {
        constexpr std::size_t hrtimIndex = 0;
        constexpr uint32_t calibrationTimeoutMs = 10;
        constexpr uint8_t prescalerRatios = 8;
        constexpr uint8_t multiplierShift = 5;
        constexpr uint8_t deadTimePrescalers = 8;
        constexpr uint8_t deadTimeMultiplierShift = 3;

        constexpr std::array<uint32_t, 6> timerIds{ {
            HRTIM_TIMERID_TIMER_A,
            HRTIM_TIMERID_TIMER_B,
            HRTIM_TIMERID_TIMER_C,
            HRTIM_TIMERID_TIMER_D,
            HRTIM_TIMERID_TIMER_E,
            HRTIM_TIMERID_TIMER_F,
        } };

        constexpr std::array<uint32_t, 6> timerUpdates{ {
            HRTIM_TIMERUPDATE_A,
            HRTIM_TIMERUPDATE_B,
            HRTIM_TIMERUPDATE_C,
            HRTIM_TIMERUPDATE_D,
            HRTIM_TIMERUPDATE_E,
            HRTIM_TIMERUPDATE_F,
        } };

        constexpr std::array<uint32_t, 6> outputs1{ { HRTIM_OUTPUT_TA1, HRTIM_OUTPUT_TB1, HRTIM_OUTPUT_TC1, HRTIM_OUTPUT_TD1, HRTIM_OUTPUT_TE1, HRTIM_OUTPUT_TF1 } };
        constexpr std::array<uint32_t, 6> outputs2{ { HRTIM_OUTPUT_TA2, HRTIM_OUTPUT_TB2, HRTIM_OUTPUT_TC2, HRTIM_OUTPUT_TD2, HRTIM_OUTPUT_TE2, HRTIM_OUTPUT_TF2 } };

        constexpr std::array<PinConfigTypeStm, 6> output1Types{ {
            PinConfigTypeStm::hrtimChA1,
            PinConfigTypeStm::hrtimChB1,
            PinConfigTypeStm::hrtimChC1,
            PinConfigTypeStm::hrtimChD1,
            PinConfigTypeStm::hrtimChE1,
            PinConfigTypeStm::hrtimChF1,
        } };

        constexpr std::array<PinConfigTypeStm, 6> output2Types{ {
            PinConfigTypeStm::hrtimChA2,
            PinConfigTypeStm::hrtimChB2,
            PinConfigTypeStm::hrtimChC2,
            PinConfigTypeStm::hrtimChD2,
            PinConfigTypeStm::hrtimChE2,
            PinConfigTypeStm::hrtimChF2,
        } };

        constexpr std::array<PinConfigTypeStm, HighResolutionPwmStm::faultInputs> faultTypes{ {
            PinConfigTypeStm::hrtimFlt1,
            PinConfigTypeStm::hrtimFlt2,
            PinConfigTypeStm::hrtimFlt3,
            PinConfigTypeStm::hrtimFlt4,
            PinConfigTypeStm::hrtimFlt5,
            PinConfigTypeStm::hrtimFlt6,
        } };

        constexpr std::array<uint32_t, HighResolutionPwmStm::faultInputs> faults{ { HRTIM_FAULT_1, HRTIM_FAULT_2, HRTIM_FAULT_3, HRTIM_FAULT_4, HRTIM_FAULT_5, HRTIM_FAULT_6 } };
        constexpr std::array<uint32_t, HighResolutionPwmStm::faultInputs> faultEnables{ {
            HRTIM_TIMFAULTENABLE_FAULT1,
            HRTIM_TIMFAULTENABLE_FAULT2,
            HRTIM_TIMFAULTENABLE_FAULT3,
            HRTIM_TIMFAULTENABLE_FAULT4,
            HRTIM_TIMFAULTENABLE_FAULT5,
            HRTIM_TIMFAULTENABLE_FAULT6,
        } };
        constexpr std::array<uint32_t, HighResolutionPwmStm::faultInputs> faultFlags{ { HRTIM_FLAG_FLT1, HRTIM_FLAG_FLT2, HRTIM_FLAG_FLT3, HRTIM_FLAG_FLT4, HRTIM_FLAG_FLT5, HRTIM_FLAG_FLT6 } };

        constexpr std::array<uint32_t, 6> delayedProtectionsDisabled{ {
            HRTIM_TIMER_A_B_C_DELAYEDPROTECTION_DISABLED,
            HRTIM_TIMER_A_B_C_DELAYEDPROTECTION_DISABLED,
            HRTIM_TIMER_A_B_C_DELAYEDPROTECTION_DISABLED,
            HRTIM_TIMER_D_E_DELAYEDPROTECTION_DISABLED,
            HRTIM_TIMER_D_E_DELAYEDPROTECTION_DISABLED,
            HRTIM_TIMER_F_DELAYEDPROTECTION_DISABLED,
        } };

        constexpr std::array<uint32_t, HighResolutionPwmStm::adcTriggers> adcTriggerIds{ {
            HRTIM_ADCTRIGGER_1,
            HRTIM_ADCTRIGGER_2,
            HRTIM_ADCTRIGGER_3,
            HRTIM_ADCTRIGGER_4,
            HRTIM_ADCTRIGGER_5,
            HRTIM_ADCTRIGGER_6,
            HRTIM_ADCTRIGGER_7,
            HRTIM_ADCTRIGGER_8,
            HRTIM_ADCTRIGGER_9,
            HRTIM_ADCTRIGGER_10,
        } };

        constexpr std::array<uint32_t, 6> adcTriggerUpdates{ {
            HRTIM_ADCTRIGGERUPDATE_TIMER_A,
            HRTIM_ADCTRIGGERUPDATE_TIMER_B,
            HRTIM_ADCTRIGGERUPDATE_TIMER_C,
            HRTIM_ADCTRIGGERUPDATE_TIMER_D,
            HRTIM_ADCTRIGGERUPDATE_TIMER_E,
            HRTIM_ADCTRIGGERUPDATE_TIMER_F,
        } };

        // The period events of one unit differ per trigger group, and triggers 2, 4, 6, 8 and 10 have none for unit E
        constexpr uint32_t noEvent = 0xffffffff;
        constexpr std::array<uint32_t, 6> periodEvents13{ {
            HRTIM_ADCTRIGGEREVENT13_TIMERA_PERIOD,
            HRTIM_ADCTRIGGEREVENT13_TIMERB_PERIOD,
            HRTIM_ADCTRIGGEREVENT13_TIMERC_PERIOD,
            HRTIM_ADCTRIGGEREVENT13_TIMERD_PERIOD,
            HRTIM_ADCTRIGGEREVENT13_TIMERE_PERIOD,
            HRTIM_ADCTRIGGEREVENT13_TIMERF_PERIOD,
        } };
        constexpr std::array<uint32_t, 6> periodEvents24{ {
            HRTIM_ADCTRIGGEREVENT24_TIMERA_PERIOD,
            HRTIM_ADCTRIGGEREVENT24_TIMERB_PERIOD,
            HRTIM_ADCTRIGGEREVENT24_TIMERC_PERIOD,
            HRTIM_ADCTRIGGEREVENT24_TIMERD_PERIOD,
            noEvent,
            HRTIM_ADCTRIGGEREVENT24_TIMERF_PERIOD,
        } };
        constexpr std::array<uint32_t, 6> periodEvents579{ {
            HRTIM_ADCTRIGGEREVENT579_TIMERA_PERIOD,
            HRTIM_ADCTRIGGEREVENT579_TIMERB_PERIOD,
            HRTIM_ADCTRIGGEREVENT579_TIMERC_PERIOD,
            HRTIM_ADCTRIGGEREVENT579_TIMERD_PERIOD,
            HRTIM_ADCTRIGGEREVENT579_TIMERE_PERIOD,
            HRTIM_ADCTRIGGEREVENT579_TIMERF_PERIOD,
        } };
        constexpr std::array<uint32_t, 6> periodEvents6810{ {
            HRTIM_ADCTRIGGEREVENT6810_TIMERA_PERIOD,
            HRTIM_ADCTRIGGEREVENT6810_TIMERB_PERIOD,
            HRTIM_ADCTRIGGEREVENT6810_TIMERC_PERIOD,
            HRTIM_ADCTRIGGEREVENT6810_TIMERD_PERIOD,
            noEvent,
            HRTIM_ADCTRIGGEREVENT6810_TIMERF_PERIOD,
        } };

        uint32_t PeriodEvent(HighResolutionPwmStm::Unit unit, uint8_t trigger)
        {
            auto index = static_cast<std::size_t>(unit);
            if (trigger <= 4)
                return trigger % 2 == 1 ? periodEvents13[index] : periodEvents24[index];

            return trigger % 2 == 1 ? periodEvents579[index] : periodEvents6810[index];
        }

        uint64_t HighResolutionClockOf(uint32_t timerClock, uint8_t prescalerRatio)
        {
            return (static_cast<uint64_t>(timerClock) << multiplierShift) >> prescalerRatio;
        }
    }

    HighResolutionTimerStm::HighResolutionTimerStm()
    {
        EnableClockHrtim(hrtimIndex);
        __HAL_RCC_HRTIM1_FORCE_RESET();
        __HAL_RCC_HRTIM1_RELEASE_RESET();

        handle.Instance = peripheralHrtim[hrtimIndex];
        handle.Init.HRTIMInterruptResquests = HRTIM_IT_NONE;
        handle.Init.SyncOptions = HRTIM_SYNCOPTION_NONE;
        really_assert(HAL_HRTIM_Init(&handle) == HAL_OK);
        really_assert(HAL_HRTIM_DLLCalibrationStart(&handle, HRTIM_CALIBRATIONRATE_3) == HAL_OK);
        really_assert(HAL_HRTIM_PollForDLLCalibration(&handle, calibrationTimeoutMs) == HAL_OK);
    }

    HighResolutionTimerStm::~HighResolutionTimerStm()
    {
        HAL_HRTIM_DeInit(&handle);
        DisableClockHrtim(hrtimIndex);
    }

    HRTIM_HandleTypeDef& HighResolutionTimerStm::Handle()
    {
        return handle;
    }

    uint32_t HighResolutionTimerStm::Clock()
    {
        RCC_ClkInitTypeDef clocks{};
        uint32_t latency = 0;
        HAL_RCC_GetClockConfig(&clocks, &latency);
        return HAL_RCC_GetPCLK2Freq() * (clocks.APB2CLKDivider == RCC_HCLK_DIV1 ? 1 : 2);
    }

    std::optional<HighResolutionPwmStm::Timing> HighResolutionPwmStm::TimingFor(uint32_t timerClock, Hertz frequency)
    {
        const uint64_t hertz = frequency.Value();
        if (hertz == 0)
            return std::nullopt;

        for (uint8_t ratio = 0; ratio != prescalerRatios; ++ratio)
        {
            auto clock = HighResolutionClockOf(timerClock, ratio);
            auto period = (clock + hertz / 2) / hertz;
            if (period <= maximumPeriod && clock >= 2 * MinimumTicks(ratio) * hertz)
                return Timing{ ratio, static_cast<uint32_t>(period) };
        }

        return std::nullopt;
    }

    uint32_t HighResolutionPwmStm::MinimumTicks(uint8_t prescalerRatio)
    {
        return std::max<uint32_t>(3, (3u << multiplierShift) >> prescalerRatio);
    }

    bool HighResolutionPwmStm::AdcTriggerAvailable(Unit unit, uint8_t trigger)
    {
        return trigger >= 1 && trigger <= adcTriggers && PeriodEvent(unit, trigger) != noEvent;
    }

    HighResolutionPwmStm::HighResolutionPwmStm(HighResolutionTimerStm& timer, Unit unit, GpioPinStm& output1, GpioPinStm& output2, const Config& config)
        : HighResolutionPwmStm(timer, unit, output1, output2, dummyPinStm, config)
    {}

    HighResolutionPwmStm::HighResolutionPwmStm(HighResolutionTimerStm& timer, Unit unit, GpioPinStm& output1, GpioPinStm& output2, GpioPinStm& faultPinToUse, const Config& config)
        : timer(timer)
        , unit(unit)
        , timerIndex(static_cast<uint32_t>(unit))
        , output1(output1, output1Types[timerIndex], 1)
        , output2(output2, output2Types[timerIndex], 1)
        , outputs(config.outputs)
    {
        really_assert(!config.deadTime || config.outputs == Outputs::complementary);
        really_assert(!config.adcTrigger || AdcTriggerAvailable(unit, *config.adcTrigger));
        really_assert(!config.faultInput || (config.faultInput->input >= 1 && config.faultInput->input <= faultInputs));
        really_assert((&faultPinToUse != &dummyPinStm) == config.faultInput.has_value());

        if (config.faultInput)
        {
            faultInput = config.faultInput->input;
            faultPin.emplace(faultPinToUse, faultTypes[*faultInput - 1], 1);
            ConfigureFault(*config.faultInput);
        }

        ConfigureTimer(config);
        if (config.outputs == Outputs::complementary)
            ConfigureDeadTime(config.deadTime.value_or(DeadTime()));
        ConfigureOutputs();
        if (config.adcTrigger)
            ConfigureAdcTrigger(*config.adcTrigger);
    }

    HighResolutionPwmStm::~HighResolutionPwmStm()
    {
        Stop();
        if (faultInput)
            HAL_HRTIM_FaultModeCtl(&timer.Handle(), faults[*faultInput - 1], HRTIM_FAULTMODECTL_DISABLED);
    }

    void HighResolutionPwmStm::SetBaseFrequency(Hertz baseFrequency)
    {
        auto newTiming = TimingFor(timer.Clock(), baseFrequency);
        really_assert(newTiming.has_value());

        // The prescaler has no preload and may only change while the counter is stopped
        const bool restart = running && timing && newTiming->prescalerRatio != timing->prescalerRatio;
        if (restart)
            HAL_HRTIM_WaveformCountStop(&timer.Handle(), timerIds[timerIndex]);

        timing = newTiming;
        HRTIM_TimeBaseCfgTypeDef timeBase{};
        timeBase.Period = timing->period;
        timeBase.RepetitionCounter = 0;
        timeBase.PrescalerRatio = timing->prescalerRatio;
        timeBase.Mode = HRTIM_MODE_CONTINUOUS;
        really_assert(HAL_HRTIM_TimeBaseConfig(&timer.Handle(), timerIndex, &timeBase) == HAL_OK);
        ApplyCompares();

        if (restart)
            HAL_HRTIM_WaveformCountStart(&timer.Handle(), timerIds[timerIndex]);
    }

    void HighResolutionPwmStm::Start(DutyCycle globalDutyCycle)
    {
        dutyCycles = { globalDutyCycle, globalDutyCycle };
        StartImpl();
    }

    void HighResolutionPwmStm::Start(DutyCycle dutyCycle1, DutyCycle dutyCycle2)
    {
        really_assert(outputs == Outputs::inPhase);
        dutyCycles = { dutyCycle1, dutyCycle2 };
        StartImpl();
    }

    void HighResolutionPwmStm::Stop()
    {
        if (!running)
            return;

        HAL_HRTIM_WaveformOutputStop(&timer.Handle(), OutputMask());
        HAL_HRTIM_WaveformCountStop(&timer.Handle(), timerIds[timerIndex]);
        running = false;
    }

    void HighResolutionPwmStm::SetPhase(DutyCycle newPhase)
    {
        really_assert(outputs == Outputs::shifted && newPhase.Value() < DutyCycle::fullScale);
        phase = newPhase;
        if (timing)
            ApplyCompares();
    }

    bool HighResolutionPwmStm::FaultActive()
    {
        return HAL_HRTIM_WaveformGetOutputState(&timer.Handle(), timerIndex, outputs1[timerIndex]) == HRTIM_OUTPUTSTATE_FAULT;
    }

    std::optional<HighResolutionPwmStm::Timing> HighResolutionPwmStm::CurrentTiming() const
    {
        return timing;
    }

    uint32_t HighResolutionPwmStm::Compare(uint8_t output) const
    {
        really_assert(timing && (output == 1 || output == 2));
        const auto& dutyCycle = outputs == Outputs::inPhase ? dutyCycles[output - 1] : dutyCycles[0];
        return Clamped(static_cast<uint32_t>(dutyCycle.ToCounts(timing->period)));
    }

    uint16_t HighResolutionPwmStm::DeadTimeTicks() const
    {
        return deadTimeTicks;
    }

    uint8_t HighResolutionPwmStm::DeadTimePrescaler() const
    {
        return deadTimePrescaler;
    }

    void HighResolutionPwmStm::ConfigureTimer(const Config& config)
    {
        HRTIM_TimerCfgTypeDef timerConfig{};
        timerConfig.InterruptRequests = HRTIM_TIM_IT_NONE;
        timerConfig.DMARequests = HRTIM_TIM_DMA_NONE;
        timerConfig.HalfModeEnable = HRTIM_HALFMODE_DISABLED;
        timerConfig.InterleavedMode = HRTIM_INTERLEAVED_MODE_DISABLED;
        timerConfig.StartOnSync = HRTIM_SYNCSTART_DISABLED;
        timerConfig.ResetOnSync = HRTIM_SYNCRESET_DISABLED;
        timerConfig.DACSynchro = HRTIM_DACSYNC_NONE;
        timerConfig.PreloadEnable = HRTIM_PRELOAD_ENABLED;
        timerConfig.UpdateGating = HRTIM_UPDATEGATING_INDEPENDENT;
        timerConfig.BurstMode = HRTIM_TIMERBURSTMODE_MAINTAINCLOCK;
        timerConfig.RepetitionUpdate = HRTIM_UPDATEONREPETITION_ENABLED;
        timerConfig.PushPull = HRTIM_TIMPUSHPULLMODE_DISABLED;
        timerConfig.FaultEnable = faultInput ? faultEnables[*faultInput - 1] : HRTIM_TIMFAULTENABLE_NONE;
        timerConfig.FaultLock = HRTIM_TIMFAULTLOCK_READWRITE;
        timerConfig.DeadTimeInsertion = config.outputs == Outputs::complementary ? HRTIM_TIMDEADTIMEINSERTION_ENABLED : HRTIM_TIMDEADTIMEINSERTION_DISABLED;
        timerConfig.DelayedProtectionMode = delayedProtectionsDisabled[timerIndex];
        timerConfig.BalancedIdleAutomaticResume = HRTIM_OUTPUTBIAR_DISABLED;
        timerConfig.UpdateTrigger = HRTIM_TIMUPDATETRIGGER_NONE;
        timerConfig.ResetTrigger = HRTIM_TIMRESETTRIGGER_NONE;
        timerConfig.ResetUpdate = HRTIM_TIMUPDATEONRESET_DISABLED;
        timerConfig.ReSyncUpdate = HRTIM_TIMERESYNC_UPDATE_UNCONDITIONAL;
        really_assert(HAL_HRTIM_WaveformTimerConfig(&timer.Handle(), timerIndex, &timerConfig) == HAL_OK);
    }

    void HighResolutionPwmStm::ConfigureOutputs()
    {
        HRTIM_OutputCfgTypeDef output{};
        output.Polarity = HRTIM_OUTPUTPOLARITY_HIGH;
        output.IdleMode = HRTIM_OUTPUTIDLEMODE_NONE;
        output.IdleLevel = HRTIM_OUTPUTIDLELEVEL_INACTIVE;
        output.FaultLevel = HRTIM_OUTPUTFAULTLEVEL_INACTIVE;
        output.ChopperModeEnable = HRTIM_OUTPUTCHOPPERMODE_DISABLED;
        output.BurstModeEntryDelayed = HRTIM_OUTPUTBURSTMODEENTRY_REGULAR;

        const bool shifted = outputs == Outputs::shifted;
        output.SetSource = shifted ? HRTIM_OUTPUTSET_TIMCMP2 : HRTIM_OUTPUTSET_TIMPER;
        output.ResetSource = HRTIM_OUTPUTRESET_TIMCMP1;
        really_assert(HAL_HRTIM_WaveformOutputConfig(&timer.Handle(), timerIndex, outputs1[timerIndex], &output) == HAL_OK);

        output.SetSource = HRTIM_OUTPUTSET_TIMPER;
        output.ResetSource = shifted ? HRTIM_OUTPUTRESET_TIMCMP3 : (outputs == Outputs::inPhase ? HRTIM_OUTPUTRESET_TIMCMP2 : HRTIM_OUTPUTRESET_TIMCMP1);
        really_assert(HAL_HRTIM_WaveformOutputConfig(&timer.Handle(), timerIndex, outputs2[timerIndex], &output) == HAL_OK);
    }

    void HighResolutionPwmStm::ConfigureDeadTime(const DeadTime& deadTime)
    {
        const auto longest = static_cast<uint64_t>(std::max(deadTime.rising, deadTime.falling).count());
        for (deadTimePrescaler = 0; deadTimePrescaler + 1 < deadTimePrescalers; ++deadTimePrescaler)
            if (longest * DeadTimeClock() / 1'000'000'000 <= maximumDeadTimeTicks)
                break;

        auto ticks = [this](std::chrono::nanoseconds duration)
        {
            return static_cast<uint32_t>(std::min<uint64_t>((static_cast<uint64_t>(duration.count()) * DeadTimeClock() + 500'000'000) / 1'000'000'000, maximumDeadTimeTicks));
        };

        HRTIM_DeadTimeCfgTypeDef config{};
        config.Prescaler = static_cast<uint32_t>(deadTimePrescaler) << HRTIM_DTR_DTPRSC_Pos;
        config.RisingValue = ticks(deadTime.rising);
        config.RisingSign = HRTIM_TIMDEADTIME_RISINGSIGN_POSITIVE;
        config.RisingLock = HRTIM_TIMDEADTIME_RISINGLOCK_WRITE;
        config.RisingSignLock = HRTIM_TIMDEADTIME_RISINGSIGNLOCK_WRITE;
        config.FallingValue = ticks(deadTime.falling);
        config.FallingSign = HRTIM_TIMDEADTIME_FALLINGSIGN_POSITIVE;
        config.FallingLock = HRTIM_TIMDEADTIME_FALLINGLOCK_WRITE;
        config.FallingSignLock = HRTIM_TIMDEADTIME_FALLINGSIGNLOCK_WRITE;
        deadTimeTicks = static_cast<uint16_t>(config.RisingValue);
        really_assert(HAL_HRTIM_DeadTimeConfig(&timer.Handle(), timerIndex, &config) == HAL_OK);
    }

    void HighResolutionPwmStm::ConfigureFault(const FaultInput& fault)
    {
        HRTIM_FaultCfgTypeDef faultConfig{};
        faultConfig.Source = HRTIM_FAULTSOURCE_DIGITALINPUT;
        faultConfig.Polarity = fault.activeHigh ? HRTIM_FAULTPOLARITY_HIGH : HRTIM_FAULTPOLARITY_LOW;
        faultConfig.Filter = HRTIM_FAULTFILTER_NONE;
        faultConfig.Lock = HRTIM_FAULTLOCK_READWRITE;
        really_assert(HAL_HRTIM_FaultConfig(&timer.Handle(), faults[fault.input - 1], &faultConfig) == HAL_OK);
        HAL_HRTIM_FaultModeCtl(&timer.Handle(), faults[fault.input - 1], HRTIM_FAULTMODECTL_ENABLED);
        __HAL_HRTIM_CLEAR_FLAG(&timer.Handle(), faultFlags[fault.input - 1]);
    }

    void HighResolutionPwmStm::ConfigureAdcTrigger(uint8_t trigger)
    {
        HRTIM_ADCTriggerCfgTypeDef adcTrigger{};
        adcTrigger.UpdateSource = adcTriggerUpdates[timerIndex];
        adcTrigger.Trigger = PeriodEvent(unit, trigger);
        really_assert(HAL_HRTIM_ADCTriggerConfig(&timer.Handle(), adcTriggerIds[trigger - 1], &adcTrigger) == HAL_OK);
    }

    void HighResolutionPwmStm::StartImpl()
    {
        really_assert(timing.has_value());
        ApplyCompares();

        // A fault disables the outputs behind the driver's back, so enabling them again here is what makes recovery through Start() work
        really_assert(HAL_HRTIM_WaveformOutputStart(&timer.Handle(), OutputMask()) == HAL_OK);
        if (!running)
            really_assert(HAL_HRTIM_WaveformCountStart(&timer.Handle(), timerIds[timerIndex]) == HAL_OK);

        running = true;
    }

    void HighResolutionPwmStm::ApplyCompares()
    {
        HRTIM_CompareCfgTypeDef compare{};
        compare.AutoDelayedMode = HRTIM_AUTODELAYEDMODE_REGULAR;
        const auto period = timing->period;

        if (outputs == Outputs::shifted)
        {
            auto start = Clamped(static_cast<uint32_t>(phase.ToCounts(period)));
            compare.CompareValue = Clamped(static_cast<uint32_t>((start + dutyCycles[0].ToCounts(period)) % period));
            really_assert(HAL_HRTIM_WaveformCompareConfig(&timer.Handle(), timerIndex, HRTIM_COMPAREUNIT_1, &compare) == HAL_OK);
            compare.CompareValue = start;
            really_assert(HAL_HRTIM_WaveformCompareConfig(&timer.Handle(), timerIndex, HRTIM_COMPAREUNIT_2, &compare) == HAL_OK);
            compare.CompareValue = Compare(1);
            really_assert(HAL_HRTIM_WaveformCompareConfig(&timer.Handle(), timerIndex, HRTIM_COMPAREUNIT_3, &compare) == HAL_OK);
        }
        else
        {
            compare.CompareValue = Compare(1);
            really_assert(HAL_HRTIM_WaveformCompareConfig(&timer.Handle(), timerIndex, HRTIM_COMPAREUNIT_1, &compare) == HAL_OK);
            compare.CompareValue = Compare(2);
            really_assert(HAL_HRTIM_WaveformCompareConfig(&timer.Handle(), timerIndex, HRTIM_COMPAREUNIT_2, &compare) == HAL_OK);
        }

        HAL_HRTIM_SoftwareUpdate(&timer.Handle(), timerUpdates[timerIndex]);
    }

    uint32_t HighResolutionPwmStm::Clamped(uint32_t ticks) const
    {
        auto minimum = MinimumTicks(timing->prescalerRatio);
        return std::clamp(ticks, minimum, timing->period - minimum);
    }

    uint32_t HighResolutionPwmStm::DeadTimeClock() const
    {
        return static_cast<uint32_t>((static_cast<uint64_t>(timer.Clock()) << deadTimeMultiplierShift) >> deadTimePrescaler);
    }

    uint32_t HighResolutionPwmStm::OutputMask() const
    {
        return outputs1[timerIndex] | outputs2[timerIndex];
    }
}

#endif
