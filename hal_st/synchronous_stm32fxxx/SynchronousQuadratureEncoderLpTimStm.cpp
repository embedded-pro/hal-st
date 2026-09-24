#include "hal_st/synchronous_stm32fxxx/SynchronousQuadratureEncoderLpTimStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_LPTIMER)

namespace
{
    uint8_t VerifiedLpTimerIndex(uint8_t oneBasedIndex)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= hal::peripheralLpTimer.size());
        return oneBasedIndex - 1;
    }
}

namespace hal
{
    SynchronousQuadratureEncoderLpTimStm::SynchronousQuadratureEncoderLpTimStm(uint8_t lpTimerOneBasedIndex, GpioPinStm& input1, GpioPinStm& input2, GpioPinStm& index, const Config& config)
        : timerIndex(VerifiedLpTimerIndex(lpTimerOneBasedIndex))
        , config(config)
        , input1(input1, PinConfigTypeStm::lpTimerInput1, lpTimerOneBasedIndex)
        , input2(input2, PinConfigTypeStm::lpTimerInput2, lpTimerOneBasedIndex)
        , index(index)
    {
        really_assert(config.resolution >= 2 && config.resolution - 1 <= 0xffffu);
        really_assert(!config.speedSamplePeriod || config.speedSamplePeriod->count() > 0);

        auto instance = peripheralLpTimer[timerIndex];
        really_assert(IS_LPTIM_ENCODER_INTERFACE_INSTANCE(instance));

        EnableClockLpTimer(timerIndex);
        handle.Instance = instance;

        handle.Init.Clock.Source = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
        handle.Init.Clock.Prescaler = LPTIM_PRESCALER_DIV1;
        handle.Init.UltraLowPowerClock.Polarity = static_cast<uint32_t>(config.decodeMode);
        handle.Init.UltraLowPowerClock.SampleTime = static_cast<uint32_t>(config.filter);
        handle.Init.Trigger.Source = LPTIM_TRIGSOURCE_SOFTWARE;
        handle.Init.OutputPolarity = LPTIM_OUTPUTPOLARITY_HIGH;
        handle.Init.UpdateMode = LPTIM_UPDATE_IMMEDIATE;
        handle.Init.CounterSource = LPTIM_COUNTERSOURCE_INTERNAL;
        handle.Init.Input1Source = LPTIM_INPUT1SOURCE_GPIO;
        handle.Init.Input2Source = LPTIM_INPUT2SOURCE_GPIO;

        auto result = HAL_LPTIM_Init(&handle);
        really_assert(result == HAL_OK);

        result = HAL_LPTIM_Encoder_Start(&handle, config.resolution - 1);
        really_assert(result == HAL_OK);

        __HAL_LPTIM_CLEAR_FLAG(&handle, LPTIM_FLAG_UP | LPTIM_FLAG_DOWN);
        previousPosition = Position();

        if (config.speedSamplePeriod)
            speedTimer.emplace(*config.speedSamplePeriod, [this]
                {
                    SampleSpeed();
                });
    }

    SynchronousQuadratureEncoderLpTimStm::~SynchronousQuadratureEncoderLpTimStm()
    {
        DisableIndex();
        speedTimer = std::nullopt;

        HAL_LPTIM_Encoder_Stop(&handle);
        HAL_LPTIM_DeInit(&handle);
        DisableClockLpTimer(timerIndex);
    }

    uint32_t SynchronousQuadratureEncoderLpTimStm::Position()
    {
        const auto counter = StableCounter();

        if (config.reverseForMirroredMounting)
            return counter == 0 ? 0 : config.resolution - counter;

        return counter;
    }

    uint32_t SynchronousQuadratureEncoderLpTimStm::Resolution()
    {
        return config.resolution;
    }

    SynchronousQuadratureEncoderLpTimStm::MotionDirection SynchronousQuadratureEncoderLpTimStm::Direction()
    {
        return CountingDown() != config.reverseForMirroredMounting ? MotionDirection::reverse : MotionDirection::forward;
    }

    uint32_t SynchronousQuadratureEncoderLpTimStm::Speed()
    {
        return speed;
    }

    uint32_t SynchronousQuadratureEncoderLpTimStm::StableCounter() const
    {
        uint32_t previous = HAL_LPTIM_ReadCounter(&handle);
        uint32_t current = HAL_LPTIM_ReadCounter(&handle);

        while (current != previous)
        {
            previous = current;
            current = HAL_LPTIM_ReadCounter(&handle);
        }

        return current;
    }

    bool SynchronousQuadratureEncoderLpTimStm::CountingDown()
    {
        const bool turnedDown = __HAL_LPTIM_GET_FLAG(&handle, LPTIM_FLAG_DOWN);
        const bool turnedUp = __HAL_LPTIM_GET_FLAG(&handle, LPTIM_FLAG_UP);

        if (turnedDown != turnedUp)
            lastKnownCountingDown = turnedDown;

        if (turnedDown || turnedUp)
            __HAL_LPTIM_CLEAR_FLAG(&handle, LPTIM_FLAG_UP | LPTIM_FLAG_DOWN);

        return lastKnownCountingDown;
    }

    uint32_t SynchronousQuadratureEncoderLpTimStm::CountsSince(uint32_t previous, uint32_t current, bool countingDown) const
    {
        if (countingDown)
            return previous >= current ? previous - current : previous + config.resolution - current;

        return current >= previous ? current - previous : current + config.resolution - previous;
    }

    void SynchronousQuadratureEncoderLpTimStm::SampleSpeed()
    {
        const auto current = Position();
        const auto counts = CountsSince(previousPosition, current, Direction() == MotionDirection::reverse);
        previousPosition = current;

        speed = static_cast<uint32_t>(static_cast<uint64_t>(counts) * 1'000'000ull / static_cast<uint64_t>(config.speedSamplePeriod->count()));
    }

    void SynchronousQuadratureEncoderLpTimStm::EnableIndex()
    {
        really_assert(&index != &dummyPinStm);

        if (indexEnabled)
            return;

        index.Config(PinConfigType::input);
        indexEnabled = true;
    }

    void SynchronousQuadratureEncoderLpTimStm::DisableIndex()
    {
        if (!indexEnabled)
            return;

        index.ResetConfig();
        indexEnabled = false;
    }

    bool SynchronousQuadratureEncoderLpTimStm::IndexAsserted() const
    {
        return indexEnabled && index.Get();
    }
}

#endif
