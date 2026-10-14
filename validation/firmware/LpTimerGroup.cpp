#include "validation/firmware/LpTimerGroup.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <array>
#include <cstddef>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_LPTIMER)

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint32_t maximumPeriod = 0xffff;
        constexpr uint32_t maximumPrescaler = 128;
        constexpr uint32_t maximumRepetition = 0xff;

        constexpr std::array<const char*, 5> openKeys{ { "period", "prescaler", "irq", "rep", "pin" } };

        constexpr std::array<uint32_t, 8> prescalers{ {
            LPTIM_PRESCALER_DIV1,
            LPTIM_PRESCALER_DIV2,
            LPTIM_PRESCALER_DIV4,
            LPTIM_PRESCALER_DIV8,
            LPTIM_PRESCALER_DIV16,
            LPTIM_PRESCALER_DIV32,
            LPTIM_PRESCALER_DIV64,
            LPTIM_PRESCALER_DIV128,
        } };

        constexpr TimerCounterGroupNames names{ "lptim.open", "lptim.start", "lptim.stop", "lptim.count", "lptim.close", " lptimclk=" };
    }

    std::optional<uint32_t> LpTimerPrescaler(uint32_t divider)
    {
        for (std::size_t shift = 0; shift != prescalers.size(); ++shift)
            if (divider == (uint32_t{ 1 } << shift))
                return prescalers[shift];

        return std::nullopt;
    }

    LpTimerFactoryStm::LpTimerFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources)
        : naming(naming)
        , resources(resources)
    {}

    uint8_t LpTimerFactoryStm::Instances() const
    {
        return 3;
    }

    infra::MemoryRange<const char* const> LpTimerFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus LpTimerFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus LpTimerFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        status = resources.Claim(Resource::lpTimer, index, owners::lpTimer);
        if (status != HilStatus::done)
            return status;

        status = marker.Claim(pins, request.pin);
        if (status != HilStatus::done)
        {
            resources.Release(Resource::lpTimer, index, owners::lpTimer);
            return status;
        }

        Construct(index, request);
        return HilStatus::done;
    }

    void LpTimerFactoryStm::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        this->onClosed = onClosed;
        Stop();

        // A dispatched update may already be queued with a pointer to the driver
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                Destroy();
            });
    }

    uint32_t LpTimerFactoryStm::Clock() const
    {
        return clock;
    }

    void LpTimerFactoryStm::Start()
    {
        if (running)
            return;

        running = true;
        if (auto freeRunning = std::get_if<hal::FreeRunningLowPowerTimerStm>(&driver))
            freeRunning->Start();
        else if (auto withInterrupt = std::get_if<hal::LowPowerTimerWithInterruptStm>(&driver))
            withInterrupt->Start([this]()
                {
                    marker.Update();
                },
                irq == TimerIrq::immediate ? hal::InterruptType::immediate : hal::InterruptType::dispatched);
    }

    void LpTimerFactoryStm::Stop()
    {
        if (!running)
            return;

        running = false;
        services::HilWithDriver(driver, [](auto& alternative)
            {
                alternative.Stop();
            });
    }

    uint32_t LpTimerFactoryStm::Counter()
    {
        uint32_t counter = 0;
        services::HilWithDriver(driver, [&counter](auto& alternative)
            {
                counter = HAL_LPTIM_ReadCounter(&alternative.Handle());
            });

        return counter;
    }

    uint32_t LpTimerFactoryStm::Interrupts() const
    {
        return marker.Updates();
    }

    HilStatus LpTimerFactoryStm::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (!LpTimerExists(index))
            return HilStatus::range;

        HilStatus status = HilStatus::done;
        arguments.Number("period", request.period, 1, maximumPeriod, status);
        arguments.Number("prescaler", request.prescaler, 1, maximumPrescaler, status);
        arguments.Number("rep", request.repetition, 0, maximumRepetition, status);
        arguments.Select("irq", request.irq, timerIrqChoices, status);
        arguments.Pin("pin", naming, request.pin, status);
        if (status != HilStatus::done)
            return status;

        if (!LpTimerPrescaler(request.prescaler))
            return HilStatus::range;

        if (request.pin && request.irq == TimerIrq::none)
            return HilStatus::usage;

        if (request.pin && !IsBonded(*request.pin))
            return HilStatus::pin;

#if defined(STM32WB) || defined(STM32G4)
        if (arguments.Has("rep"))
            return HilStatus::unsupported;
#endif

        // ARRM interrupts on every period, whatever the repetition counter
        if (request.irq != TimerIrq::none && LpTimerClock(index) / (request.prescaler * (request.period + 1)) > maximumUpdateInterruptRate)
            return HilStatus::range;

        return HilStatus::done;
    }

    void LpTimerFactoryStm::Construct(uint8_t index, const Request& request)
    {
        this->index = index;
        clock = LpTimerClock(index);
        irq = request.irq;
        running = false;

        const hal::LowPowerTimerBaseStm::Timing timing{ request.period, request.repetition, *LpTimerPrescaler(request.prescaler) };
        if (request.irq == TimerIrq::none)
            driver.emplace<hal::FreeRunningLowPowerTimerStm>(index, timing);
        else
            driver.emplace<hal::LowPowerTimerWithInterruptStm>(index, timing);
    }

    void LpTimerFactoryStm::Destroy()
    {
        driver.emplace<std::monostate>();
        marker.Release();
        resources.Release(Resource::lpTimer, index, owners::lpTimer);
        onClosed();
    }

    LpTimerGroup::LpTimerGroup(services::HilContext& context, LpTimerFactoryStm& factory)
        : TimerCounterGroup(context, factory, owners::lpTimer, names)
    {}
}

#endif
