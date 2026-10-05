#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal_st/stm32fxxx/LpTimerStm.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/hil/HilCommand.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/TimerGroup.hpp"
#include <cstdint>
#include <optional>
#include <variant>

#if defined(HAS_PERIPHERAL_LPTIMER)

namespace validation
{
    std::optional<uint32_t> LpTimerPrescaler(uint32_t divider);

    class LpTimerFactoryStm
        : public TimerCounterFactory
    {
    public:
        LpTimerFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources);

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
            uint32_t period = 999;
            uint32_t prescaler = 1;
            uint32_t repetition = 0;
            TimerIrq irq = TimerIrq::dispatched;
            std::optional<HilPinId> pin;
        };

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        void Construct(uint8_t index, const Request& request);
        void Destroy();

    private:
        const services::HilPinNaming& naming;
        ResourceAllocation& resources;
        std::variant<std::monostate, hal::FreeRunningLowPowerTimerStm, hal::LowPowerTimerWithInterruptStm> driver;
        TimerMarker marker;
        uint8_t index = 0;
        uint32_t clock = 0;
        TimerIrq irq = TimerIrq::none;
        bool running = false;
        infra::AutoResetFunction<void()> onClosed;
    };

    class LpTimerGroup
        : public TimerCounterGroup
    {
    public:
        LpTimerGroup(services::HilContext& context, LpTimerFactoryStm& factory);
    };
}

#endif
