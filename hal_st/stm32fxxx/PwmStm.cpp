#include "hal_st/stm32fxxx/PwmStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

#if defined(HAS_PERIPHERAL_TIMER)

namespace
{
    constexpr std::array channelPinConfigs{
        hal::PinConfigTypeStm::timerChannel1,
        hal::PinConfigTypeStm::timerChannel2,
        hal::PinConfigTypeStm::timerChannel3,
        hal::PinConfigTypeStm::timerChannel4
    };

    constexpr std::array complementaryChannelPinConfigs{
        hal::PinConfigTypeStm::timerChannel1N,
        hal::PinConfigTypeStm::timerChannel2N,
        hal::PinConfigTypeStm::timerChannel3N
    };

    constexpr std::array timerChannels{
        TIM_CHANNEL_1,
        TIM_CHANNEL_2,
        TIM_CHANNEL_3,
        TIM_CHANNEL_4
    };

    constexpr uint32_t maximumDeadTimeTicks = 1008;

    uint8_t VerifiedTimerIndex(uint8_t oneBasedIndex)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= hal::peripheralTimer.size());
        return oneBasedIndex - 1;
    }

    hal::PinConfigTypeStm ChannelPinConfig(uint8_t zeroBasedChannel)
    {
        really_assert(zeroBasedChannel < channelPinConfigs.size());
        return channelPinConfigs[zeroBasedChannel];
    }

    hal::PinConfigTypeStm ComplementaryChannelPinConfig(uint8_t zeroBasedChannel)
    {
        really_assert(zeroBasedChannel < complementaryChannelPinConfigs.size());
        return complementaryChannelPinConfigs[zeroBasedChannel];
    }

    uint32_t TimerChannel(uint8_t zeroBasedChannel)
    {
        really_assert(zeroBasedChannel < timerChannels.size());
        return timerChannels[zeroBasedChannel];
    }

    uint32_t ClockDivisionFactor(uint32_t clockDivision)
    {
        if (clockDivision == TIM_CLOCKDIVISION_DIV2)
            return 2;
        if (clockDivision == TIM_CLOCKDIVISION_DIV4)
            return 4;
        return 1;
    }

    constexpr uint32_t DivideRoundingUp(uint64_t numerator, uint64_t denominator)
    {
        return static_cast<uint32_t>((numerator + denominator - 1) / denominator);
    }

    bool IsCenterAligned(uint32_t counterMode)
    {
        return counterMode == TIM_COUNTERMODE_CENTERALIGNED1 || counterMode == TIM_COUNTERMODE_CENTERALIGNED2 || counterMode == TIM_COUNTERMODE_CENTERALIGNED3;
    }

    bool IsOnApb2(const TIM_TypeDef* instance)
    {
#if defined(TIM1)
        if (instance == TIM1)
            return true;
#endif
#if defined(TIM8)
        if (instance == TIM8)
            return true;
#endif
#if defined(TIM9)
        if (instance == TIM9)
            return true;
#endif
#if defined(TIM10)
        if (instance == TIM10)
            return true;
#endif
#if defined(TIM11)
        if (instance == TIM11)
            return true;
#endif
#if defined(TIM15)
        if (instance == TIM15)
            return true;
#endif
#if defined(TIM16)
        if (instance == TIM16)
            return true;
#endif
#if defined(TIM17)
        if (instance == TIM17)
            return true;
#endif
#if defined(TIM20)
        if (instance == TIM20)
            return true;
#endif
        static_cast<void>(instance);
        return false;
    }

    // F4/F7/H5 can select TIMPRE, under which the kernel clock reaches HCLK for APB
    // divisions up to four instead of the usual twice-PCLK.
    bool TimerPrescalerSelected()
    {
#if defined(RCC_DCKCFGR_TIMPRE)
        return (RCC->DCKCFGR & RCC_DCKCFGR_TIMPRE) != 0;
#elif defined(RCC_DCKCFGR1_TIMPRE)
        return (RCC->DCKCFGR1 & RCC_DCKCFGR1_TIMPRE) != 0;
#elif defined(RCC_CFGR1_TIMPRE)
        return (RCC->CFGR1 & RCC_CFGR1_TIMPRE) != 0;
#else
        return false;
#endif
    }
}

namespace hal
{
    uint8_t PwmStmBase::EncodeDeadTime(std::chrono::nanoseconds deadTime, uint32_t timerClockFrequency, uint32_t clockDivision)
    {
        if (deadTime.count() <= 0 || timerClockFrequency == 0)
            return 0;

        const auto deadTimeClock = timerClockFrequency / ClockDivisionFactor(clockDivision);
        if (deadTimeClock == 0)
            return 0;

        // Range-checked before multiplying: the product would otherwise overflow and wrap a
        // very long request into a dangerously short dead-time.
        if (static_cast<uint64_t>(deadTime.count()) > static_cast<uint64_t>(maximumDeadTimeTicks) * 1'000'000'000ull / deadTimeClock)
            return 0xff;

        const auto ticks = DivideRoundingUp(static_cast<uint64_t>(deadTime.count()) * deadTimeClock, 1'000'000'000ull);

        if (ticks <= 127)
            return static_cast<uint8_t>(ticks);

        if (ticks <= 254)
            return static_cast<uint8_t>(0x80u | (DivideRoundingUp(ticks, 2) - 64));

        if (ticks <= 504)
            return static_cast<uint8_t>(0xc0u | (DivideRoundingUp(ticks, 8) - 32));

        if (ticks <= maximumDeadTimeTicks)
            return static_cast<uint8_t>(0xe0u | (DivideRoundingUp(ticks, 16) - 32));

        return 0xff;
    }

    std::chrono::nanoseconds PwmStmBase::DecodeDeadTime(uint8_t deadTimeGenerator, uint32_t timerClockFrequency, uint32_t clockDivision)
    {
        if (timerClockFrequency == 0)
            return std::chrono::nanoseconds{ 0 };

        const auto deadTimeClock = timerClockFrequency / ClockDivisionFactor(clockDivision);
        if (deadTimeClock == 0)
            return std::chrono::nanoseconds{ 0 };

        uint32_t ticks = 0;

        if ((deadTimeGenerator & 0x80u) == 0)
            ticks = deadTimeGenerator;
        else if ((deadTimeGenerator & 0xc0u) == 0x80u)
            ticks = (64 + (deadTimeGenerator & 0x3fu)) * 2;
        else if ((deadTimeGenerator & 0xe0u) == 0xc0u)
            ticks = (32 + (deadTimeGenerator & 0x1fu)) * 8;
        else
            ticks = (32 + (deadTimeGenerator & 0x1fu)) * 16;

        return std::chrono::nanoseconds{ static_cast<int64_t>(static_cast<uint64_t>(ticks) * 1'000'000'000ull / deadTimeClock) };
    }

    PwmStmBase::Channel::Channel(uint8_t timerOneBasedIndex, const ChannelConfig& config)
        : index(config.channel - 1)
        , complementary(&config.complementaryPin != &dummyPinStm)
        , pin(config.pin, ChannelPinConfig(config.channel - 1), timerOneBasedIndex)
    {
        if (complementary)
            complementaryPin.emplace(config.complementaryPin, ComplementaryChannelPinConfig(config.channel - 1), timerOneBasedIndex);
    }

    PwmStmBase::PwmStmBase(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channelConfigs, GpioPinStm& breakPinToUse, const Config& config)
        : timerIndex(VerifiedTimerIndex(timerOneBasedIndex))
        , config(config)
    {
        really_assert(!channelConfigs.empty() && channelConfigs.size() <= maxChannels);

        auto instance = peripheralTimer[timerIndex];

        for (const auto& channelConfig : channelConfigs)
        {
            really_assert(channelConfig.channel >= 1 && channelConfig.channel <= maxChannels);
            really_assert(IS_TIM_CCX_INSTANCE(instance, TimerChannel(channelConfig.channel - 1)));
            really_assert(&channelConfig.complementaryPin == &dummyPinStm || IS_TIM_CCXN_INSTANCE(instance, TimerChannel(channelConfig.channel - 1)));

            idleStateRequested = idleStateRequested || channelConfig.idleStateHigh || channelConfig.complementaryIdleStateHigh;

            channels.emplace_back(timerOneBasedIndex, channelConfig);
        }

        if (&breakPinToUse != &dummyPinStm)
        {
            really_assert(config.breakInput && IS_TIM_BREAK_INSTANCE(instance));
            breakPin.emplace(breakPinToUse, PinConfigTypeStm::timerBreak, timerOneBasedIndex);
        }

        really_assert(config.alignment == Alignment::edgeAligned || IS_TIM_COUNTER_MODE_SELECT_INSTANCE(instance));
        really_assert((!config.deadTime && !config.breakInput && !idleStateRequested) || IS_TIM_BREAK_INSTANCE(instance));

        EnableClockTimer(timerIndex);
        handle.Instance = instance;

        ConfigureTimeBase();

        if (config.triggerOutput)
            ConfigureTriggerOutput();

        for (std::size_t i = 0; i != channels.size(); ++i)
            ConfigureChannel(channels[i], channelConfigs[i]);

        if (config.deadTime || config.breakInput || idleStateRequested)
            ConfigureBreakAndDeadTime();

        if (breakPin)
            ConfigureBreakInputSource();
    }

    PwmStmBase::~PwmStmBase()
    {
        StopImpl();
        HAL_TIM_PWM_DeInit(&handle);
        DisableClockTimer(timerIndex);
    }

    void PwmStmBase::ConfigureTimeBase()
    {
        handle.Init.Prescaler = config.prescaler;
        handle.Init.CounterMode = static_cast<uint32_t>(config.alignment);
        handle.Init.Period = 0;
        handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        handle.Init.AutoReloadPreload = config.preloadEnabled ? TIM_AUTORELOAD_PRELOAD_ENABLE : TIM_AUTORELOAD_PRELOAD_DISABLE;
        handle.Init.RepetitionCounter = 0;

        auto result = HAL_TIM_PWM_Init(&handle);
        really_assert(result == HAL_OK);

        // HAL_TIM_PWM_Init leaves SMCR alone, so a timer left in encoder mode would stay
        // clocked from its input pins.
        TIM_ClockConfigTypeDef clockSource{};
        clockSource.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
        result = HAL_TIM_ConfigClockSource(&handle, &clockSource);
        really_assert(result == HAL_OK);
    }

    void PwmStmBase::ConfigureTriggerOutput()
    {
        TIM_MasterConfigTypeDef master{};
        master.MasterOutputTrigger = static_cast<uint32_t>(*config.triggerOutput);
        master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

        auto result = HAL_TIMEx_MasterConfigSynchronization(&handle, &master);
        really_assert(result == HAL_OK);
    }

    void PwmStmBase::ConfigureChannel(const Channel& channel, const ChannelConfig& channelConfig)
    {
        TIM_OC_InitTypeDef init{};
        init.OCMode = TIM_OCMODE_PWM1;
        init.Pulse = 0;
        init.OCPolarity = channelConfig.inverted ? TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
        init.OCNPolarity = channelConfig.complementaryInverted ? TIM_OCNPOLARITY_LOW : TIM_OCNPOLARITY_HIGH;
        init.OCFastMode = TIM_OCFAST_DISABLE;
        init.OCIdleState = channelConfig.idleStateHigh ? TIM_OCIDLESTATE_SET : TIM_OCIDLESTATE_RESET;
        init.OCNIdleState = channelConfig.complementaryIdleStateHigh ? TIM_OCNIDLESTATE_SET : TIM_OCNIDLESTATE_RESET;

        auto result = HAL_TIM_PWM_ConfigChannel(&handle, &init, TimerChannel(channel.index));
        really_assert(result == HAL_OK);

        // HAL_TIM_PWM_ConfigChannel enables compare preload unconditionally, so honouring
        // preloadEnabled means undoing it here.
        if (config.preloadEnabled)
            __HAL_TIM_ENABLE_OCxPRELOAD(&handle, TimerChannel(channel.index));
        else
            __HAL_TIM_DISABLE_OCxPRELOAD(&handle, TimerChannel(channel.index));
    }

    void PwmStmBase::ConfigureBreakAndDeadTime()
    {
        TIM_BreakDeadTimeConfigTypeDef init{};

        // The idle levels only reach the pins while the timer keeps controlling them after
        // MOE clears, which is what the off-state selections enable.
        init.OffStateRunMode = TIM_OSSR_ENABLE;
        init.OffStateIDLEMode = TIM_OSSI_ENABLE;
        init.LockLevel = TIM_LOCKLEVEL_OFF;
        init.DeadTime = config.deadTime ? EncodeDeadTime(config.deadTime->duration, TimerClockFrequency(), handle.Init.ClockDivision) : 0;

        if (config.breakInput)
        {
            init.BreakState = TIM_BREAK_ENABLE;
            init.BreakPolarity = config.breakInput->activeHigh ? TIM_BREAKPOLARITY_HIGH : TIM_BREAKPOLARITY_LOW;
            init.BreakFilter = std::min<uint32_t>(config.breakInput->filter, 0xf);
            init.AutomaticOutput = config.breakInput->automaticOutputEnable ? TIM_AUTOMATICOUTPUT_ENABLE : TIM_AUTOMATICOUTPUT_DISABLE;
        }
        else
        {
            init.BreakState = TIM_BREAK_DISABLE;
            init.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
            init.BreakFilter = 0;
            init.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
        }

#if defined(TIM_BDTR_BK2E)
        init.Break2State = TIM_BREAK2_DISABLE;
        init.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
        init.Break2Filter = 0;
#endif

        auto result = HAL_TIMEx_ConfigBreakDeadTime(&handle, &init);
        really_assert(result == HAL_OK);
    }

    void PwmStmBase::ConfigureBreakInputSource()
    {
#if defined(TIM_BREAKINPUTSOURCE_BKIN)
        really_assert(IS_TIM_BREAKSOURCE_INSTANCE(handle.Instance));

        // BDTR.BKE alone only arms the reaction; the external pin is a separate mux source.
        TIMEx_BreakInputConfigTypeDef source{};
        source.Source = TIM_BREAKINPUTSOURCE_BKIN;
        source.Enable = TIM_BREAKINPUTSOURCE_ENABLE;
        source.Polarity = config.breakInput->activeHigh ? TIM_BREAKINPUTSOURCE_POLARITY_HIGH : TIM_BREAKINPUTSOURCE_POLARITY_LOW;

        auto result = HAL_TIMEx_ConfigBreakInput(&handle, TIM_BREAKINPUT_BRK, &source);
        really_assert(result == HAL_OK);
#endif
    }

    uint32_t PwmStmBase::TimerClockFrequency() const
    {
        const auto hclk = HAL_RCC_GetHCLKFreq();
        const auto peripheralClock = IsOnApb2(handle.Instance) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();

        if (peripheralClock == 0)
            return hclk;

        const auto apbDivider = hclk / peripheralClock;

        if (TimerPrescalerSelected())
            return apbDivider <= 4 ? hclk : peripheralClock * 4;

        return apbDivider == 1 ? peripheralClock : peripheralClock * 2;
    }

    uint32_t PwmStmBase::MaximumCompare() const
    {
        return IS_TIM_32B_COUNTER_INSTANCE(handle.Instance) ? 0xffffffffu : 0xffffu;
    }

    void PwmStmBase::SetBaseFrequencyImpl(Hertz baseFrequency)
    {
        really_assert(baseFrequency.Value() != 0);

        const auto counterClock = TimerClockFrequency() / (static_cast<uint32_t>(config.prescaler) + 1);
        auto ticksPerPeriod = counterClock / baseFrequency.Value();

        if (IsCenterAligned(handle.Init.CounterMode))
            ticksPerPeriod /= 2;

        really_assert(ticksPerPeriod >= 2);
        really_assert(ticksPerPeriod - 1 <= MaximumCompare());

        handle.Init.Period = ticksPerPeriod - 1;
        __HAL_TIM_SET_AUTORELOAD(&handle, handle.Init.Period);

        for (auto& channel : channels)
            SetDutyCycle(channel, channel.dutyCycle);
    }

    void PwmStmBase::SetDutyCycle(Channel& channel, Percent dutyCycle)
    {
        really_assert(dutyCycle.Value() <= 100);
        really_assert(handle.Init.Period != 0);

        channel.dutyCycle = dutyCycle;

        // Clamped rather than taken modulo: at the widest period the full-duty value is one
        // past what the compare register holds, and would otherwise wrap to no output at all.
        const auto period = static_cast<uint64_t>(handle.Init.Period) + 1;
        const auto compare = std::min<uint64_t>(period * dutyCycle.Value() / 100, MaximumCompare());

        __HAL_TIM_SET_COMPARE(&handle, TimerChannel(channel.index), static_cast<uint32_t>(compare));
    }

    void PwmStmBase::StartImpl(infra::MemoryRange<const Percent> dutyCycles)
    {
        really_assert(dutyCycles.size() == channels.size());
        really_assert(handle.Init.Period != 0);

        for (std::size_t i = 0; i != channels.size(); ++i)
            SetDutyCycle(channels[i], dutyCycles[i]);

        if (started)
        {
            // A break event clears MOE behind the driver's back, so re-arming it here is what
            // makes recovery through Start() work when automatic output enable is off.
            if (IS_TIM_BREAK_INSTANCE(handle.Instance))
                __HAL_TIM_MOE_ENABLE(&handle);

            return;
        }

        for (const auto& channel : channels)
        {
            auto result = HAL_TIM_PWM_Start(&handle, TimerChannel(channel.index));
            really_assert(result == HAL_OK);

            if (channel.complementary)
            {
                result = HAL_TIMEx_PWMN_Start(&handle, TimerChannel(channel.index));
                really_assert(result == HAL_OK);
            }
        }

        started = true;
    }

    void PwmStmBase::StopImpl()
    {
        if (!started)
            return;

        for (const auto& channel : channels)
        {
            if (channel.complementary)
            {
                auto result = HAL_TIMEx_PWMN_Stop(&handle, TimerChannel(channel.index));
                really_assert(result == HAL_OK);
            }

            auto result = HAL_TIM_PWM_Stop(&handle, TimerChannel(channel.index));
            really_assert(result == HAL_OK);
        }

        started = false;
    }

    PwmStm::PwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, const Config& config)
        : PwmStmBase(timerOneBasedIndex, channels, dummyPinStm, config)
    {}

    PwmStm::PwmStm(uint8_t timerOneBasedIndex, infra::MemoryRange<const ChannelConfig> channels, GpioPinStm& breakPin, const Config& config)
        : PwmStmBase(timerOneBasedIndex, channels, breakPin, config)
    {}

    void PwmStm::SetBaseFrequency(Hertz baseFrequency)
    {
        SetBaseFrequencyImpl(baseFrequency);
    }

    void PwmStm::Start(Percent globalDutyCycle)
    {
        const std::array dutyCycles{ globalDutyCycle };
        StartImpl(dutyCycles);
    }

    void PwmStm::Start(Percent dutyCycle1, Percent dutyCycle2)
    {
        const std::array dutyCycles{ dutyCycle1, dutyCycle2 };
        StartImpl(dutyCycles);
    }

    void PwmStm::Start(Percent dutyCycle1, Percent dutyCycle2, Percent dutyCycle3)
    {
        const std::array dutyCycles{ dutyCycle1, dutyCycle2, dutyCycle3 };
        StartImpl(dutyCycles);
    }

    void PwmStm::Start(Percent dutyCycle1, Percent dutyCycle2, Percent dutyCycle3, Percent dutyCycle4)
    {
        const std::array dutyCycles{ dutyCycle1, dutyCycle2, dutyCycle3, dutyCycle4 };
        StartImpl(dutyCycles);
    }

    void PwmStm::Stop()
    {
        StopImpl();
    }
}

#endif
