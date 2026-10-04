#pragma once

#include "hal_st/stm32fxxx/TimerStm.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilSingleInstanceGroup.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    enum class TimerIrq : uint8_t
    {
        none,
        immediate,
        dispatched,
    };

    inline constexpr std::array<services::HilChoice<TimerIrq>, 3> timerIrqChoices{ {
        { "none", TimerIrq::none },
        { "immediate", TimerIrq::immediate },
        { "dispatched", TimerIrq::dispatched },
    } };

    class TimerMarker
    {
    public:
        services::HilStatus Claim(services::HilPinOwner& pins, const std::optional<HilPinId>& id);
        void Update();
        uint32_t Updates() const;
        void Release();

    private:
        hal::GpioPin* pin = nullptr;
        std::atomic<uint32_t> updates{ 0 };
    };

    class TimerCounterFactory
        : public services::HilInstanceFactory
    {
    protected:
        TimerCounterFactory() = default;
        TimerCounterFactory(const TimerCounterFactory& other) = delete;
        TimerCounterFactory& operator=(const TimerCounterFactory& other) = delete;
        ~TimerCounterFactory() = default;

    public:
        virtual services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins) = 0;
        virtual uint32_t Clock() const = 0;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual uint32_t Counter() = 0;
        virtual uint32_t Interrupts() const = 0;
    };

    struct TimerCounterGroupNames
    {
        const char* open;
        const char* start;
        const char* stop;
        const char* count;
        const char* close;
        const char* clock;
    };

    class TimerCounterGroup
        : public services::HilSingleInstanceGroup
    {
    public:
        TimerCounterGroup(services::HilContext& context, TimerCounterFactory& factory, services::HilOwner owner, const TimerCounterGroupNames& names);

        infra::MemoryRange<const Command> Commands() override;

    protected:
        services::HilStatus OpenInstance(uint8_t index, const services::HilArguments& arguments) override;
        void Opened(services::HilResponse::Line& line) const override;
        void CloseInstance() override;

    private:
        services::HilStatus Start(const services::HilArguments& arguments);
        services::HilStatus Stop(const services::HilArguments& arguments);
        services::HilStatus Count(const services::HilArguments& arguments);
        services::HilStatus FindOpened(const services::HilArguments& arguments) const;

    private:
        TimerCounterFactory& factory;
        const char* clockKey;
        std::array<Command, 5> commands;
    };

    class TimerFactoryStm
        : public TimerCounterFactory
    {
    public:
        TimerFactoryStm(const services::HilPinNaming& naming, TimerAllocation& timers);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins) override;
        uint32_t Clock() const override;
        void Start() override;
        void Stop() override;
        uint32_t Counter() override;
        uint32_t Interrupts() const override;

    private:
        struct Request
        {
            uint32_t prescaler = 0;
            uint32_t period = 999;
            TimerIrq irq = TimerIrq::dispatched;
            hal::TimerBaseStm::CounterMode mode = hal::TimerBaseStm::CounterMode::up;
            std::optional<HilPinId> pin;
        };

        services::HilStatus Evaluate(uint8_t timer, const services::HilArguments& arguments, Request& request) const;
        void Construct(uint8_t timer, const Request& request);
        void Destroy();

    private:
        const services::HilPinNaming& naming;
        TimerAllocation& timers;
        std::variant<std::monostate, hal::FreeRunningTimerStm, hal::TimerWithInterruptStm> driver;
        TimerMarker marker;
        uint8_t timer = 0;
        uint32_t clock = 0;
        TimerIrq irq = TimerIrq::none;
        bool running = false;
        infra::AutoResetFunction<void()> onClosed;
    };

    class TimerGroup
        : public TimerCounterGroup
    {
    public:
        TimerGroup(services::HilContext& context, TimerFactoryStm& factory);
    };
}
