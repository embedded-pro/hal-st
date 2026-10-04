#include "validation/firmware/AdcFactory.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/Tokenizer.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <algorithm>
#include <limits>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr std::array<const char*, 5> openKeys{ { "pins", "sampling", "timer", "rate", "trgo" } };

        constexpr uint32_t defaultRate = 1000;
        constexpr uint32_t maximumRate = 100000;
        constexpr uint64_t minimumTicks = 2;

        static_assert(board::adcDma.dma == 1);

        // A Stop from the event loop must not interleave with the DMA interrupt, which disables the ADC itself in one-shot mode and stops a timer-triggered sequence from EMIL's last-run callback
        class InterruptsDisabled
        {
        public:
            InterruptsDisabled()
                : primask(__get_PRIMASK())
            {
                __disable_irq();
            }

            InterruptsDisabled(const InterruptsDisabled& other) = delete;
            InterruptsDisabled& operator=(const InterruptsDisabled& other) = delete;

            ~InterruptsDisabled()
            {
                __set_PRIMASK(primask);
            }

        private:
            uint32_t primask;
        };

        bool IsTriggerTimer(uint8_t timer)
        {
            return std::ranges::find(board::adcTriggerTimers, timer) != board::adcTriggerTimers.end();
        }

        uint64_t Distance(uint64_t a, uint64_t b)
        {
            return a > b ? a - b : b - a;
        }

        // PwmStm drives TRGO only, so a timer the ADC reaches through TRGO2 alone (TIM1 on the STM32WBA ADC4) cannot pace it
        std::optional<uint32_t> PwmTriggerSource(uint8_t timer)
        {
            switch (timer)
            {
#if defined(ADC_EXTERNALTRIG_T1_TRGO)
                case 1:
                    return ADC_EXTERNALTRIG_T1_TRGO;
#endif
#if defined(ADC_EXTERNALTRIG_T2_TRGO)
                case 2:
                    return ADC_EXTERNALTRIG_T2_TRGO;
#endif
                default:
                    return std::nullopt;
            }
        }
    }

    // The smallest prescaler that fits is at most 1 / maximumTicks off the best divider; searching every prescaler for that one blocks the event loop for tens of milliseconds
    hal::TimerBaseStm::Timing TriggerTiming(uint8_t timer, uint32_t rate)
    {
        const uint64_t clock = TimerClock(timer);
        const uint64_t maximumTicks = IS_TIM_32B_COUNTER_INSTANCE(hal::peripheralTimer[timer - 1]) ? uint64_t{ 1 } << 32 : uint64_t{ 1 } << 16;

        const uint64_t prescale = clock / (rate * (maximumTicks + 1)) + 1;
        const uint64_t floorTicks = std::max(clock / (prescale * rate), minimumTicks);
        const uint64_t ceilTicks = std::min(floorTicks + 1, maximumTicks);
        const bool roundUp = Distance(clock, prescale * ceilTicks * rate) * floorTicks < Distance(clock, prescale * floorTicks * rate) * ceilTicks;

        return { static_cast<uint32_t>(prescale - 1), static_cast<uint32_t>((roundUp ? ceilTicks : floorTicks) - 1) };
    }

    template<class Mode>
    AdcFactoryStm::Sequence::Sequence(infra::MemoryRange<uint16_t> samples, infra::MemoryRange<hal::AnalogPinStm> inputs, hal::AdcStm& converter, hal::DmaStm::ReceiveStream& receiveStream, ChannelConfigs configs, Mode mode)
        : hal::AdcDmaMultiChannelStmBase(samples, inputs, converter, receiveStream, mode)
    {
        ConfigureChannels(configs);
    }

    void AdcFactoryStm::Sequence::Stop()
    {
        InterruptsDisabled interruptsDisabled;
        hal::AdcDmaMultiChannelStmBase::Stop();
    }

    AdcFactoryStm::TriggeredSequence::TriggeredSequence(infra::MemoryRange<uint16_t> samples, infra::MemoryRange<hal::AnalogPinStm> inputs, hal::AdcStm& converter, hal::DmaStm::ReceiveStream& receiveStream, ChannelConfigs configs, uint8_t oneBasedTimer, hal::TimerBaseStm::Timing timing)
        : hal::AdcTimerTriggeredBase(converter, oneBasedTimer, timing)
        , Sequence(samples, inputs, converter, receiveStream, configs, hal::AdcDmaMultiChannelStmBase::Unlimited{})
    {
        ReconfigureTrigger();
    }

    void AdcFactoryStm::TriggeredSequence::Measure(const infra::Function<void(Samples)>& onDone)
    {
        measuring = true;
        StartTimer();
        Sequence::Measure(onDone);
    }

    void AdcFactoryStm::TriggeredSequence::Stop()
    {
        InterruptsDisabled interruptsDisabled;
        Sequence::Stop();
        StopTimer();
        measuring = false;
    }

    bool AdcFactoryStm::TriggeredSequence::Measuring() const
    {
        return measuring;
    }

    AdcFactoryStm::PwmTriggeredSequence::PwmTriggeredSequence(infra::MemoryRange<uint16_t> samples, infra::MemoryRange<hal::AnalogPinStm> inputs, hal::AdcStm& converter, hal::DmaStm::ReceiveStream& receiveStream, ChannelConfigs configs, uint32_t triggerSource)
        : Sequence(samples, inputs, converter, receiveStream, configs, hal::AdcDmaMultiChannelStmBase::Unlimited{})
    {
        HAL_ADC_Stop(&converter.Handle());

        LL_ADC_REG_SetTriggerSource(converter.Handle().Instance, triggerSource);
#if defined(ADC_CFGR1_EXTEN) || defined(ADC_CFGR_EXTEN)
        LL_ADC_REG_SetTriggerEdge(converter.Handle().Instance, ADC_EXTERNALTRIGCONVEDGE_RISING);
#endif
    }

    void AdcFactoryStm::PwmTriggeredSequence::Measure(const infra::Function<void(Samples)>& onDone)
    {
        measuring = true;
        Sequence::Measure(onDone);
    }

    void AdcFactoryStm::PwmTriggeredSequence::Stop()
    {
        Sequence::Stop();
        measuring = false;
    }

    bool AdcFactoryStm::PwmTriggeredSequence::Measuring() const
    {
        return measuring;
    }

    void AdcFactoryStm::RepeatedConversion::Attach(Sequence& sequence, Samples samples)
    {
        this->sequence = &sequence;
        this->samples = samples;
    }

    void AdcFactoryStm::RepeatedConversion::Detach()
    {
        if (running)
            Stop();

        sequence = nullptr;
    }

    void AdcFactoryStm::RepeatedConversion::Measure(const infra::Function<void(Samples)>& onDone)
    {
        this->onDone = onDone;
        running = true;
        Convert();
    }

    void AdcFactoryStm::RepeatedConversion::Stop()
    {
        running = false;
        ++conversion;
        sequence->Stop();
    }

    void AdcFactoryStm::RepeatedConversion::Convert()
    {
        const auto current = ++conversion;
        sequence->Measure([this, current](Samples)
            {
                infra::EventDispatcher::Instance().Schedule([this, current]()
                    {
                        Deliver(current);
                    });
            });
    }

    // Each completion is tagged, so one that was already scheduled when Stop() ran never reports nor re-arms a later measurement
    void AdcFactoryStm::RepeatedConversion::Deliver(uint32_t completed)
    {
        if (!running || completed != conversion)
            return;

        onDone(samples);

        if (running && completed == conversion)
            Convert();
    }

    AdcFactoryStm::AdcFactoryStm(const services::HilPinNaming& naming, hal::DmaStm& dma, TimerAllocation& timers, ResourceAllocation& resources)
        : naming(naming)
        , dma(dma)
        , timers(timers)
        , resources(resources)
    {}

    std::size_t AdcFactoryStm::KeyPositionals() const
    {
        return 1;
    }

    HilStatus AdcFactoryStm::ParseKey(const services::HilArguments& arguments, uint16_t& key) const
    {
        uint32_t index = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, index, 0, std::numeric_limits<uint16_t>::max(), status);
        if (status == HilStatus::done && index != board::adc)
            status = HilStatus::range;

        key = static_cast<uint16_t>(index);
        return status;
    }

    infra::MemoryRange<const char* const> AdcFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus AdcFactoryStm::Prepare(uint16_t, const services::HilArguments& arguments)
    {
        Request request;
        return Parse(arguments, request);
    }

    HilStatus AdcFactoryStm::Open(std::size_t, uint16_t, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilAdcHandle& handle)
    {
        Request request;
        HilStatus status = Parse(arguments, request);

        std::array<hal::GpioPin*, maximumPins> claimed{};
        for (std::size_t i = 0; i != request.count && status == HilStatus::done; ++i)
            status = pins.ClaimAnalog(request.pins[i], claimed[i]);

        if (status == HilStatus::done)
            status = ClaimResources();

        if (status == HilStatus::done && request.timer)
        {
            status = timers.Claim(*request.timer, services::HilOwners::adc);
            if (status != HilStatus::done)
                ReleaseResources();
        }

        if (status != HilStatus::done)
            return status;

        timer = request.timer;
        Construct(request, claimed, handle);
        return HilStatus::done;
    }

    void AdcFactoryStm::Close(std::size_t, uint16_t, const infra::Function<void()>& onClosed)
    {
        repeated.Detach();

        if (auto triggered = std::get_if<TriggeredSequence>(&sequence); triggered != nullptr && triggered->Measuring())
            triggered->Stop();

        if (auto triggered = std::get_if<PwmTriggeredSequence>(&sequence); triggered != nullptr && triggered->Measuring())
            triggered->Stop();

        sequence.emplace<std::monostate>();
        analogPins.clear();
        stream.reset();
        adc.reset();

        if (timer)
            timers.Release(*timer, services::HilOwners::adc);
        timer.reset();
        ReleaseResources();

        onClosed();
    }

    HilStatus AdcFactoryStm::Parse(const services::HilArguments& arguments, Request& request) const
    {
        if (arguments.Has("rate") && !arguments.Has("timer"))
            return HilStatus::usage;

        HilStatus status = HilStatus::done;
        request.samplingTime = board::adcDefaultSamplingTime;
        request.rate = defaultRate;

        arguments.Select("sampling", request.samplingTime, board::adcSamplingTimes, status);
        if (arguments.Has("timer"))
        {
            uint32_t number = 0;
            arguments.Number("timer", number, 0, static_cast<uint32_t>(hal::peripheralTimer.size()), status);
            request.timer = static_cast<uint8_t>(number);
        }
        arguments.Number("rate", request.rate, 1, maximumRate, status);
        if (arguments.Has("trgo"))
        {
            uint32_t number = 0;
            arguments.Number("trgo", number, 0, static_cast<uint32_t>(hal::peripheralTimer.size()), status);
            request.triggerTimer = static_cast<uint8_t>(number);
        }
        if (status != HilStatus::done)
            return status;

        if (request.triggerTimer && (request.timer || arguments.Has("rate")))
            return HilStatus::usage;

        status = ParsePins(arguments, request);
        if (status != HilStatus::done)
            return status;

        if (request.timer && !TimerExists(*request.timer))
            return HilStatus::range;

        if (request.timer && !IsTriggerTimer(*request.timer))
            return HilStatus::unsupported;

        if (request.triggerTimer && !TimerExists(*request.triggerTimer))
            return HilStatus::range;

        if (request.triggerTimer)
            return EvaluateTrigger(*request.triggerTimer);

        return HilStatus::done;
    }

    HilStatus AdcFactoryStm::EvaluateTrigger(uint8_t timer) const
    {
        const auto owner = timers.Owner(timer);
        if (!PwmTriggerSource(timer) || !owner)
            return HilStatus::unsupported;

        if (*owner != services::HilOwners::pwm)
            return HilStatus::busy;

        return HilStatus::done;
    }

    HilStatus AdcFactoryStm::ClaimResources()
    {
        HilStatus status = resources.Claim(Resource::adc, board::adc, services::HilOwners::adc);
        if (status == HilStatus::done)
            status = resources.Claim(Resource::dma1, board::adcDma.channel, services::HilOwners::adc);

        if (status != HilStatus::done)
            ReleaseResources();

        return status;
    }

    void AdcFactoryStm::ReleaseResources()
    {
        resources.Release(Resource::adc, board::adc, services::HilOwners::adc);
        resources.Release(Resource::dma1, board::adcDma.channel, services::HilOwners::adc);
    }

    HilStatus AdcFactoryStm::ParsePins(const services::HilArguments& arguments, Request& request) const
    {
        const auto list = arguments.Key("pins");
        if (!list)
            return HilStatus::usage;

        const auto entries = static_cast<std::size_t>(std::ranges::count(*list, ',')) + 1;
        if (entries > maximumPins)
            return HilStatus::range;

        const infra::Tokenizer tokens(*list, ',');
        if (tokens.Size() != entries)
            return HilStatus::pin;

        for (std::size_t i = 0; i != entries; ++i)
        {
            const auto pin = services::HilArguments::ParsePin(tokens.Token(i), naming);
            if (!pin || !SupportsAnalog(*pin))
                return HilStatus::pin;

            request.pins[i] = *pin;
        }

        request.count = entries;
        return HilStatus::done;
    }

    void AdcFactoryStm::Construct(const Request& request, const std::array<hal::GpioPin*, maximumPins>& claimed, services::HilAdcHandle& handle)
    {
        auto& converter = adc.emplace(board::adc);
        auto& receiveStream = stream.emplace(dma, hal::DmaChannelId(board::adcDma.dma, board::adcDma.channel, board::adcDmaRequest));

        for (std::size_t i = 0; i != request.count; ++i)
        {
            analogPins.emplace_back(PinOrDummy(claimed[i]));
            configs[i] = hal::detail::AdcStmChannelConfig{ request.samplingTime, false };
        }

        const auto samples = infra::MakeRange(buffer.data(), buffer.data() + request.count);
        const ChannelConfigs channels = infra::MakeRange(configs.data(), configs.data() + request.count);
        handle.samplesPerRun = request.count;

        if (request.timer)
        {
            handle.adc = &sequence.emplace<TriggeredSequence>(samples, infra::MakeRange(analogPins), converter, receiveStream, channels, *request.timer, TriggerTiming(*request.timer, request.rate));
            return;
        }

        if (request.triggerTimer)
        {
            handle.adc = &sequence.emplace<PwmTriggeredSequence>(samples, infra::MakeRange(analogPins), converter, receiveStream, channels, *PwmTriggerSource(*request.triggerTimer));
            return;
        }

        repeated.Attach(sequence.emplace<Sequence>(samples, infra::MakeRange(analogPins), converter, receiveStream, channels, hal::AdcDmaMultiChannelStmBase::OneShot{}), samples);
        handle.adc = &repeated;
    }
}
