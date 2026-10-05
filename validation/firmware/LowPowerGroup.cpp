#include "validation/firmware/LowPowerGroup.hpp"
#include "BoardProfile.hpp"
#include "hal_st/stm32fxxx/TimerStm.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <cstddef>
#include <optional>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;

        constexpr std::array<const char*, 4> enterKeys{ { "wake", "edge", "marker", "timeout" } };

        constexpr std::array<HilChoice<hal::PowerMode>, 2> modes{ {
            { "sleep", hal::PowerMode::sleep },
            { "deep", hal::PowerMode::deepSleep },
        } };

        constexpr std::array<HilChoice<bool>, 2> edges{ {
            { "rising", true },
            { "falling", false },
        } };

        constexpr uint32_t maximumTimeoutMs = 10000;
        constexpr uint32_t ticksPerSecond = 1000000;
        constexpr uint32_t ticksPerUpdate = 1000;
        constexpr std::size_t nvicWords = sizeof(NVIC_Type::ISER) / sizeof(uint32_t);

        static_assert(board::scaffoldTimer == 17);
#if defined(STM32WB)
        constexpr IRQn_Type scaffoldIrq = TIM1_TRG_COM_TIM17_IRQn;
#else
        constexpr IRQn_Type scaffoldIrq = TIM17_IRQn;
#endif

        IRQn_Type ExtiIrq(uint8_t line)
        {
#if defined(STM32WB)
            if (line >= 10)
                return EXTI15_10_IRQn;
            if (line >= 5)
                return EXTI9_5_IRQn;
#endif
            return static_cast<IRQn_Type>(EXTI0_IRQn + line);
        }

        bool ExtiPending(uint32_t mask)
        {
            return __HAL_GPIO_EXTI_GET_IT(mask) != 0;
        }
    }

    LowPowerCommands::LowPowerCommands(services::HilContext& context, TimerAllocation& timers)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , timers(timers)
        , pins(context.pins, owners::lowPower)
        , lowPower([this]()
              {
                  ++restored;
              })
        , commands{ {
              services::HilBind<LowPowerCommands, &LowPowerCommands::Enter>("lpm.enter", "<sleep|deep> [wake=<pin>] [edge=rising|falling] [marker=<pin>] [timeout=<ms>]", *this, context.response),
          } }
    {}

    infra::MemoryRange<const LowPowerCommands::Command> LowPowerCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus LowPowerCommands::Enter(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(enterKeys)))
            return HilStatus::usage;

        Request request;
        auto status = Parse(arguments, request);
        if (status != HilStatus::done)
            return status;

        hal::GpioPin* wake = nullptr;
        hal::GpioPin* marker = nullptr;
        status = Claim(request, wake, marker);
        if (status != HilStatus::done)
            return status;

        restored = 0;
        const auto outcome = Sleep(request, *marker);

        wake->DisableInterrupt();
        wake->ResetConfig();
        marker->ResetConfig();
        pins.Release();
        timers.Release(board::scaffoldTimer, owners::lowPower);

        if (!outcome.woke)
            return HilStatus::timeout;

        context.response.Ok() << " woke=exti restored=" << restored.load() << " us=" << outcome.us;
        return HilStatus::done;
    }

    HilStatus LowPowerCommands::Parse(const services::HilArguments& arguments, Request& request) const
    {
        std::optional<HilPinId> wake;
        std::optional<HilPinId> marker;
        auto status = HilStatus::done;
        arguments.SelectAt(0, request.mode, modes, status);
        arguments.Select("edge", request.rising, edges, status);
        arguments.Number("timeout", request.timeoutMs, 1, maximumTimeoutMs, status);
        arguments.Pin("wake", context.naming, wake, status);
        arguments.Pin("marker", context.naming, marker, status);
        if (status != HilStatus::done)
            return status;

        request.wake = wake.value_or(board::lowPowerDefaults.wake);
        request.marker = marker.value_or(board::lowPowerDefaults.marker);
        if (request.wake == request.marker)
            return HilStatus::usage;

        if (!IsBonded(request.wake) || !IsBonded(request.marker))
            return HilStatus::pin;

        if (!context.pins.Factory().SupportsInterrupt(request.wake))
            return HilStatus::unsupported;

        return HilStatus::done;
    }

    HilStatus LowPowerCommands::Claim(const Request& request, hal::GpioPin*& wake, hal::GpioPin*& marker)
    {
        auto status = timers.Claim(board::scaffoldTimer, owners::lowPower);
        if (status != HilStatus::done)
            return status;

        services::HilPinOptions wakeOptions;
        wakeOptions.pull = request.rising ? services::HilPull::down : services::HilPull::up;
        status = pins.Claim(request.wake, services::HilPinPool::Use::exclusive, wake, wakeOptions);
        if (status == HilStatus::done)
            status = pins.Claim(request.marker, services::HilPinPool::Use::exclusive, marker);
        if (status != HilStatus::done)
        {
            pins.Release();
            timers.Release(board::scaffoldTimer, owners::lowPower);
            return status;
        }

        woke = false;
        wake->Config(hal::PinConfigType::input);
        marker->Config(hal::PinConfigType::output, true);
        wake->EnableInterrupt([this]()
            {
                woke = true;
            },
            request.rising ? hal::InterruptTrigger::risingEdge : hal::InterruptTrigger::fallingEdge, hal::InterruptType::immediate);
        return HilStatus::done;
    }

    LowPowerCommands::Outcome LowPowerCommands::Sleep(const Request& request, hal::GpioPin& marker)
    {
        // CYCCNT stops while the core sleeps: the scaffold timer counts the window in microseconds instead
        hal::TimerWithInterruptStm timer{ board::scaffoldTimer, { TimerClock(board::scaffoldTimer) / ticksPerSecond - 1, ticksPerUpdate - 1 } };
        auto& handle = timer.Handle();
        const uint32_t wakeMask = 1u << request.wake.index;
        const auto wakeIrq = ExtiIrq(request.wake.index);
        uint32_t updates = 0;

        __HAL_TIM_CLEAR_FLAG(&handle, TIM_FLAG_UPDATE);
        timer.Start([]() {}, hal::InterruptType::immediate);

        // With PRIMASK set WFI still returns on any enabled interrupt that becomes pending, so only the wake line and
        // the scaffold timer stay enabled; their handlers run once PRIMASK is restored
        const auto primask = __get_PRIMASK();
        __disable_irq();

        std::array<uint32_t, nvicWords> enabled;
        for (std::size_t word = 0; word != enabled.size(); ++word)
        {
            enabled[word] = NVIC->ISER[word];
            for (uint32_t bit = 0; bit != 32; ++bit)
            {
                const auto irq = static_cast<IRQn_Type>(word * 32 + bit);
                if ((enabled[word] & (1u << bit)) != 0 && irq != wakeIrq && irq != scaffoldIrq)
                    HAL_NVIC_DisableIRQ(irq);
            }
        }

        const bool tick = (SysTick->CTRL & SysTick_CTRL_TICKINT_Msk) != 0;
        HAL_SuspendTick();
        // A tick that pended before TICKINT was cleared would end every WFI at once
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

        __HAL_TIM_SET_COUNTER(&handle, 0);
        __HAL_TIM_CLEAR_FLAG(&handle, TIM_FLAG_UPDATE);
        HAL_NVIC_ClearPendingIRQ(scaffoldIrq);
        marker.Set(false);

        while (!woke && !ExtiPending(wakeMask) && updates != request.timeoutMs)
        {
            lowPower.Enter(request.mode);

            if (__HAL_TIM_GET_FLAG(&handle, TIM_FLAG_UPDATE) != 0)
            {
                __HAL_TIM_CLEAR_FLAG(&handle, TIM_FLAG_UPDATE);
                HAL_NVIC_ClearPendingIRQ(scaffoldIrq);
                ++updates;
            }
        }

        marker.Set(true);

        const uint32_t counter = __HAL_TIM_GET_COUNTER(&handle);
        const bool wrapped = __HAL_TIM_GET_FLAG(&handle, TIM_FLAG_UPDATE) != 0 && counter < ticksPerUpdate / 2;
        Outcome outcome{ woke || ExtiPending(wakeMask), (updates + (wrapped ? 1 : 0)) * ticksPerUpdate + counter };

        timer.Stop();
        __HAL_TIM_CLEAR_FLAG(&handle, TIM_FLAG_UPDATE);
        HAL_NVIC_ClearPendingIRQ(scaffoldIrq);

        if (tick)
            HAL_ResumeTick();

        for (std::size_t word = 0; word != enabled.size(); ++word)
            for (uint32_t bit = 0; bit != 32; ++bit)
                if ((enabled[word] & (1u << bit)) != 0)
                    HAL_NVIC_EnableIRQ(static_cast<IRQn_Type>(word * 32 + bit));

        __set_PRIMASK(primask);
        return outcome;
    }
}
