#include "validation/firmware/TimerGroup.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using CounterMode = hal::TimerBaseStm::CounterMode;
        using services::HilChoice;
        using services::HilStatus;

        constexpr uint32_t maximumPrescaler = 0xffff;

        constexpr std::array<const char*, 5> openKeys{ { "prescaler", "period", "irq", "mode", "pin" } };

        constexpr std::array<HilChoice<CounterMode>, 2> modeChoices{ {
            { "up", CounterMode::up },
            { "down", CounterMode::down },
        } };

        constexpr TimerCounterGroupNames names{ "tim.open", "tim.start", "tim.stop", "tim.count", "tim.close", " timclk=" };

        TIM_TypeDef* Instance(uint8_t timer)
        {
            return hal::peripheralTimer[timer - 1];
        }

        uint32_t MaximumPeriod(uint8_t timer)
        {
            return IS_TIM_32B_COUNTER_INSTANCE(Instance(timer)) ? 0xffffffffu : 0xffffu;
        }
    }

    HilStatus TimerMarker::Claim(services::HilPinOwner& pins, const std::optional<HilPinId>& id)
    {
        updates = 0;
        pin = nullptr;
        if (!id)
            return HilStatus::done;

        hal::GpioPin* claimed = nullptr;
        HilStatus status = pins.Claim(*id, services::HilPinPool::Use::exclusive, claimed);
        if (status != HilStatus::done)
            return status;

        claimed->Config(hal::PinConfigType::output, false);
        pin = claimed;
        return HilStatus::done;
    }

    void TimerMarker::Update()
    {
        if (pin != nullptr)
            pin->Set(!pin->GetOutputLatch());

        updates.fetch_add(1);
    }

    uint32_t TimerMarker::Updates() const
    {
        return updates.load();
    }

    void TimerMarker::Release()
    {
        if (pin != nullptr)
            pin->ResetConfig();

        pin = nullptr;
    }

    TimerCounterGroup::TimerCounterGroup(services::HilContext& context, TimerCounterFactory& factory, services::HilOwner owner, const TimerCounterGroupNames& names)
        : services::HilSingleInstanceGroup(context, factory, owner)
        , factory(factory)
        , clockKey(names.clock)
        , commands{ {
              OpenCommand(names.open, "<index> [key=value]..."),
              services::HilBind<TimerCounterGroup, &TimerCounterGroup::Start>(names.start, "<index>", *this, context.response),
              services::HilBind<TimerCounterGroup, &TimerCounterGroup::Stop>(names.stop, "<index>", *this, context.response),
              services::HilBind<TimerCounterGroup, &TimerCounterGroup::Count>(names.count, "<index>", *this, context.response),
              CloseCommand(names.close, "<index>"),
          } }
    {}

    infra::MemoryRange<const TimerCounterGroup::Command> TimerCounterGroup::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus TimerCounterGroup::OpenInstance(uint8_t index, const services::HilArguments& arguments)
    {
        return factory.Open(index, arguments, Pins());
    }

    void TimerCounterGroup::Opened(services::HilResponse::Line& line) const
    {
        line << clockKey << factory.Clock();
    }

    void TimerCounterGroup::CloseInstance()
    {}

    HilStatus TimerCounterGroup::Start(const services::HilArguments& arguments)
    {
        HilStatus status = FindOpened(arguments);
        if (status != HilStatus::done)
            return status;

        factory.Start();
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus TimerCounterGroup::Stop(const services::HilArguments& arguments)
    {
        HilStatus status = FindOpened(arguments);
        if (status != HilStatus::done)
            return status;

        factory.Stop();
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus TimerCounterGroup::Count(const services::HilArguments& arguments)
    {
        HilStatus status = FindOpened(arguments);
        if (status != HilStatus::done)
            return status;

        Context().response.Ok() << " cnt=" << factory.Counter() << " irqs=" << factory.Interrupts();
        return HilStatus::done;
    }

    HilStatus TimerCounterGroup::FindOpened(const services::HilArguments& arguments) const
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        return Instance().Find(arguments);
    }

    TimerFactoryStm::TimerFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers)
        : naming(naming)
        , timers(timers)
    {}

    uint8_t TimerFactoryStm::Instances() const
    {
        return 18;
    }

    infra::MemoryRange<const char* const> TimerFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus TimerFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus TimerFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        status = timers.Claim(index, owners::timer);
        if (status != HilStatus::done)
            return status;

        status = marker.Claim(pins, request.pin);
        if (status != HilStatus::done)
        {
            timers.Release(index, owners::timer);
            return status;
        }

        Construct(index, request);
        return HilStatus::done;
    }

    void TimerFactoryStm::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        this->onClosed = onClosed;
        Stop();

        // A dispatched update may already be queued with a pointer to the driver
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                Destroy();
            });
    }

    uint32_t TimerFactoryStm::Clock() const
    {
        return clock;
    }

    void TimerFactoryStm::Start()
    {
        if (running)
            return;

        running = true;
        if (auto freeRunning = std::get_if<hal::FreeRunningTimerStm>(&driver))
            freeRunning->Start();
        else if (auto withInterrupt = std::get_if<hal::TimerWithInterruptStm>(&driver))
            withInterrupt->Start([this]()
                {
                    marker.Update();
                },
                irq == TimerIrq::immediate ? hal::InterruptType::immediate : hal::InterruptType::dispatched);
    }

    void TimerFactoryStm::Stop()
    {
        if (!running)
            return;

        running = false;
        services::HilWithDriver(driver, [](auto& alternative)
            {
                alternative.Stop();
            });
    }

    uint32_t TimerFactoryStm::Counter()
    {
        uint32_t counter = 0;
        services::HilWithDriver(driver, [&counter](auto& alternative)
            {
                counter = __HAL_TIM_GET_COUNTER(&alternative.Handle());
            });

        return counter;
    }

    uint32_t TimerFactoryStm::Interrupts() const
    {
        return marker.Updates();
    }

    HilStatus TimerFactoryStm::Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const
    {
        if (!TimerExists(timer))
            return HilStatus::range;

        HilStatus status = HilStatus::done;
        arguments.Number("prescaler", request.prescaler, 0, maximumPrescaler, status);
        arguments.Number("period", request.period, 1, MaximumPeriod(timer), status);
        arguments.Select("irq", request.irq, timerIrqChoices, status);
        arguments.Select("mode", request.mode, modeChoices, status);
        arguments.Pin("pin", naming, request.pin, status);
        if (status != HilStatus::done)
            return status;

        if (request.pin && request.irq == TimerIrq::none)
            return HilStatus::usage;

        if (request.pin && !IsBonded(*request.pin))
            return HilStatus::pin;

        if (request.mode == CounterMode::down && (request.irq != TimerIrq::none || !IS_TIM_COUNTER_MODE_SELECT_INSTANCE(Instance(timer))))
            return HilStatus::unsupported;

        if (request.irq != TimerIrq::none && TimerClock(timer) / ((uint64_t{ request.prescaler } + 1) * (uint64_t{ request.period } + 1)) > maximumUpdateInterruptRate)
            return HilStatus::range;

        return HilStatus::done;
    }

    void TimerFactoryStm::Construct(uint8_t timer, const Request& request)
    {
        this->timer = timer;
        clock = TimerClock(timer);
        irq = request.irq;
        running = false;

        const hal::TimerBaseStm::Timing timing{ request.prescaler, request.period };
        if (request.irq == TimerIrq::none)
            driver.emplace<hal::FreeRunningTimerStm>(timer, timing, hal::TimerBaseStm::Config{ request.mode, std::nullopt });
        else
            driver.emplace<hal::TimerWithInterruptStm>(timer, timing);
    }

    void TimerFactoryStm::Destroy()
    {
        driver.emplace<std::monostate>();
        marker.Release();
        timers.Release(timer, owners::timer);
        onClosed();
    }

    TimerGroup::TimerGroup(services::HilContext& context, TimerFactoryStm& factory)
        : TimerCounterGroup(context, factory, owners::timer, names)
    {}
}
