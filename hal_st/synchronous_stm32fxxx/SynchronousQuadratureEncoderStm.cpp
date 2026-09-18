#include "hal_st/synchronous_stm32fxxx/SynchronousQuadratureEncoderStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

#if defined(HAS_PERIPHERAL_TIMER)

namespace
{
    uint8_t VerifiedTimerIndex(uint8_t oneBasedIndex)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= hal::peripheralTimer.size());
        return oneBasedIndex - 1;
    }
}

namespace hal
{
    SynchronousQuadratureEncoderStm::SynchronousQuadratureEncoderStm(uint8_t timerOneBasedIndex, GpioPinStm& phaseA, GpioPinStm& phaseB, GpioPinStm& index, const Config& config)
        : timerIndex(VerifiedTimerIndex(timerOneBasedIndex))
        , config(config)
        , phaseA(phaseA, PinConfigTypeStm::timerChannel1, timerOneBasedIndex)
        , phaseB(phaseB, PinConfigTypeStm::timerChannel2, timerOneBasedIndex)
        , index(index)
    {
        really_assert(config.resolution >= 2);
        really_assert(config.offset < config.resolution);
        really_assert(!config.speedSamplePeriod || config.speedSamplePeriod->count() > 0);

        auto instance = peripheralTimer[timerIndex];
        really_assert(IS_TIM_ENCODER_INTERFACE_INSTANCE(instance));

        const uint32_t maximumPeriod = IS_TIM_32B_COUNTER_INSTANCE(instance) ? 0xffffffffu : 0xffffu;
        really_assert(config.resolution - 1 <= maximumPeriod);

        EnableClockTimer(timerIndex);
        handle.Instance = instance;

        handle.Init.Prescaler = 0;
        handle.Init.CounterMode = TIM_COUNTERMODE_UP;
        handle.Init.Period = config.resolution - 1;
        handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
        handle.Init.RepetitionCounter = 0;

        TIM_Encoder_InitTypeDef encoder{};
        encoder.EncoderMode = static_cast<uint32_t>(config.decodeMode);
        encoder.IC1Polarity = config.invertPhaseA ? TIM_ICPOLARITY_FALLING : TIM_ICPOLARITY_RISING;
        encoder.IC1Selection = TIM_ICSELECTION_DIRECTTI;
        encoder.IC1Prescaler = TIM_ICPSC_DIV1;
        encoder.IC1Filter = std::min<uint32_t>(config.filter, 0xf);
        encoder.IC2Polarity = config.invertPhaseB ? TIM_ICPOLARITY_FALLING : TIM_ICPOLARITY_RISING;
        encoder.IC2Selection = TIM_ICSELECTION_DIRECTTI;
        encoder.IC2Prescaler = TIM_ICPSC_DIV1;
        encoder.IC2Filter = std::min<uint32_t>(config.filter, 0xf);

        auto result = HAL_TIM_Encoder_Init(&handle, &encoder);
        really_assert(result == HAL_OK);

        result = HAL_TIM_Encoder_Start(&handle, TIM_CHANNEL_ALL);
        really_assert(result == HAL_OK);

        __HAL_TIM_SET_COUNTER(&handle, config.offset);
        previousPosition = config.offset;

        if (config.speedSamplePeriod)
            speedTimer.emplace(*config.speedSamplePeriod, [this]
                {
                    SampleSpeed();
                });
    }

    SynchronousQuadratureEncoderStm::~SynchronousQuadratureEncoderStm()
    {
        DisableIndex();
        speedTimer = std::nullopt;

        HAL_TIM_Encoder_Stop(&handle, TIM_CHANNEL_ALL);
        HAL_TIM_Encoder_DeInit(&handle);
        DisableClockTimer(timerIndex);
    }

    uint32_t SynchronousQuadratureEncoderStm::Position()
    {
        return __HAL_TIM_GET_COUNTER(&handle);
    }

    uint32_t SynchronousQuadratureEncoderStm::Resolution()
    {
        return config.resolution;
    }

    SynchronousQuadratureEncoderStm::MotionDirection SynchronousQuadratureEncoderStm::Direction()
    {
        return __HAL_TIM_IS_TIM_COUNTING_DOWN(&handle) ? MotionDirection::reverse : MotionDirection::forward;
    }

    uint32_t SynchronousQuadratureEncoderStm::Speed()
    {
        return speed;
    }

    // Taken in the direction the counter is running, so a wrap at the resolution boundary
    // reads as a small step rather than a revolution the other way.
    uint32_t SynchronousQuadratureEncoderStm::CountsSince(uint32_t previous, uint32_t current, bool countingDown) const
    {
        if (countingDown)
            return previous >= current ? previous - current : previous + config.resolution - current;

        return current >= previous ? current - previous : current + config.resolution - previous;
    }

    void SynchronousQuadratureEncoderStm::SampleSpeed()
    {
        const auto current = Position();
        const auto counts = CountsSince(previousPosition, current, __HAL_TIM_IS_TIM_COUNTING_DOWN(&handle));
        previousPosition = current;

        speed = static_cast<uint32_t>(static_cast<uint64_t>(counts) * 1'000'000ull / static_cast<uint64_t>(config.speedSamplePeriod->count()));
    }

    void SynchronousQuadratureEncoderStm::EnableIndex()
    {
        really_assert(&index != &dummyPinStm);

        if (indexEnabled)
            return;

        index.Config(PinConfigType::input);
        indexEnabled = true;
    }

    void SynchronousQuadratureEncoderStm::DisableIndex()
    {
        if (!indexEnabled)
            return;

        index.ResetConfig();
        indexEnabled = false;
    }

    bool SynchronousQuadratureEncoderStm::IndexAsserted() const
    {
        return indexEnabled && index.Get();
    }
}

#endif
