#include "validation/firmware/HsemGroup.hpp"

#if defined(STM32WB)

#include "BoardProfile.hpp"
#include "hal_st/stm32fxxx/TimerStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousHardwareSemaphoreStm.hpp"
#include "stm32wbxx_ll_hsem.h"
#include "validation/firmware/HsemMaster.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include <chrono>
#include <optional>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint32_t maximumHoldMs = 60000;
        constexpr uint32_t minimumHoldUs = 2;
        constexpr uint32_t maximumHoldUs = 65536;
        constexpr uint32_t ticksPerSecond = 1000000;
        constexpr uint32_t lockHoldProcess = 1;
    }

    HsemCommands::HsemCommands(services::HilContext& context, TimerAllocation& timers, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , timers(timers)
        , resources(resources)
        , commands{ {
              services::HilBind<HsemCommands, &HsemCommands::Take>("hsem.take", "<n> procid=<1..255> [hold=<ms>]", *this, context.response),
              services::HilBind<HsemCommands, &HsemCommands::Release>("hsem.release", "<n> procid=<1..255>", *this, context.response),
              services::HilBind<HsemCommands, &HsemCommands::Status>("hsem.status", "<n>", *this, context.response),
              services::HilBind<HsemCommands, &HsemCommands::Lock>("hsem.lock", "<n> [hold=<us>]", *this, context.response),
              services::HilBind<HsemCommands, &HsemCommands::Mine>("hsem.mine", "<n>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const HsemCommands::Command> HsemCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus HsemCommands::Take(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "procid", "hold" }))
            return HilStatus::usage;

        uint32_t semaphore = 0;
        uint32_t process = 0;
        uint32_t hold = 0;
        auto status = ParseProcess(arguments, semaphore, process);
        arguments.Number("hold", hold, 1, maximumHoldMs, status);
        if (status != HilStatus::done)
            return status;

        HsemMaster();
        if (hold != 0 && holdTimer.Armed())
            return HilStatus::busy;

        if (HAL_HSEM_Take(semaphore, process) != HAL_OK)
            return HilStatus::busy;

        if (hold != 0)
        {
            heldSemaphore = semaphore;
            heldProcess = process;
            holdTimer.Start(std::chrono::milliseconds(hold), [this]()
                {
                    HAL_HSEM_Release(heldSemaphore, heldProcess);
                });
        }

        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus HsemCommands::Release(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "procid" }))
            return HilStatus::usage;

        uint32_t semaphore = 0;
        uint32_t process = 0;
        auto status = ParseProcess(arguments, semaphore, process);
        if (status != HilStatus::done)
            return status;

        HsemMaster();
        HAL_HSEM_Release(semaphore, process);
        if (holdTimer.Armed() && heldSemaphore == semaphore && heldProcess == process)
            holdTimer.Cancel();

        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus HsemCommands::Status(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        uint32_t semaphore = 0;
        auto status = HilStatus::done;
        arguments.NumberAt(0, semaphore, 0, HSEM_SEMID_MAX, status);
        if (status != HilStatus::done)
            return status;

        HsemMaster();
        context.response.Ok() << " locked=" << LL_HSEM_IsSemaphoreLocked(HSEM, semaphore) << " core=" << (LL_HSEM_GetCoreId(HSEM, semaphore) >> HSEM_R_COREID_Pos) << " procid=" << LL_HSEM_GetProcessId(HSEM, semaphore);
        return HilStatus::done;
    }

    HilStatus HsemCommands::Lock(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "hold" }))
            return HilStatus::usage;

        uint32_t semaphore = 0;
        uint32_t hold = 0;
        auto status = HilStatus::done;
        arguments.NumberAt(0, semaphore, 0, HSEM_SEMID_MAX, status);
        arguments.Number("hold", hold, minimumHoldUs, maximumHoldUs, status);
        if (status != HilStatus::done)
            return status;

        HsemMaster();
        status = resources.Claim(Resource::hsem, 0, owners::scaffold);
        if (status == HilStatus::done && hold != 0)
        {
            status = timers.Claim(board::scaffoldTimer, owners::scaffold);
            if (status != HilStatus::done)
                resources.Release(Resource::hsem, 0, owners::scaffold);
        }
        if (status != HilStatus::done)
            return status;

        // The wait blocks the event loop: a semaphore held by anything but the scaffold timer would never be freed
        uint32_t waited = 0;
        if (LL_HSEM_IsSemaphoreLocked(HSEM, semaphore) != 0)
            status = HilStatus::busy;
        else
            waited = Wait(semaphore, hold);

        if (hold != 0)
            timers.Release(board::scaffoldTimer, owners::scaffold);
        resources.Release(Resource::hsem, 0, owners::scaffold);
        if (status != HilStatus::done)
            return status;

        context.response.Ok() << " waited=" << waited;
        return HilStatus::done;
    }

    HilStatus HsemCommands::Mine(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        uint32_t semaphore = 0;
        auto status = HilStatus::done;
        arguments.NumberAt(0, semaphore, 0, HSEM_SEMID_MAX, status);
        if (status != HilStatus::done)
            return status;

        const bool mine = HsemMaster().IsLockedByCurrentCore(static_cast<hal::Semaphore>(semaphore));
        context.response.Ok() << " mine=" << static_cast<uint32_t>(mine ? 1 : 0);
        return HilStatus::done;
    }

    HilStatus HsemCommands::ParseProcess(const services::HilArguments& arguments, uint32_t& semaphore, uint32_t& process) const
    {
        if (!arguments.Has("procid"))
            return HilStatus::usage;

        auto status = HilStatus::done;
        arguments.NumberAt(0, semaphore, 0, HSEM_SEMID_MAX, status);
        arguments.Number("procid", process, 1, HSEM_PROCESSID_MAX, status);
        return status;
    }

    uint32_t HsemCommands::Wait(uint32_t semaphore, uint32_t holdUs)
    {
        std::optional<hal::TimerWithInterruptStm> releaser;

        if (holdUs != 0)
        {
            HAL_HSEM_Take(semaphore, lockHoldProcess);

            auto& timer = releaser.emplace(board::scaffoldTimer, hal::TimerBaseStm::Timing{ TimerClock(board::scaffoldTimer) / ticksPerSecond - 1, holdUs - 1 });
            __HAL_TIM_CLEAR_FLAG(&timer.Handle(), TIM_FLAG_UPDATE);
            timer.Start([&timer, semaphore]()
                {
                    HAL_HSEM_Release(semaphore, lockHoldProcess);
                    timer.Stop();
                },
                hal::InterruptType::immediate);
        }

        stopwatch.Start();
        hal::SynchronousHardwareSemaphoreStm lock{ HsemMaster(), static_cast<hal::Semaphore>(semaphore) };
        return stopwatch.ElapsedUs();
    }
}

#endif
