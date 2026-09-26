#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "generated/stm32fxxx/PinoutTableDefault.hpp"

extern "C" uint32_t SystemCoreClock;

namespace main_
{
    StmEventInfrastructure::StmEventInfrastructure(infra::Duration tickDuration)
        : gpio(hal::pinoutTableDefaultStm, hal::analogTableDefaultStm)
        , systemTick(SystemCoreClock, tickDuration)
    {}

    void StmEventInfrastructure::Run()
    {
        eventDispatcher.Run();
    }

    LowPowerStmEventInfrastructure::LowPowerStmEventInfrastructure(infra::Duration tickDuration)
        : eventDispatcher(lowPowerStrategy)
        , gpio(hal::pinoutTableDefaultStm, hal::analogTableDefaultStm)
        , systemTick(SystemCoreClock, tickDuration)
    {}

    void LowPowerStmEventInfrastructure::Run()
    {
        eventDispatcher.Run();
    }
}
