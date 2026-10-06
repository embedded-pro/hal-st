#include "hal_st/instantiations/EvalUi.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DefaultClockEvalH757I.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "services/peripheral/DebugLed.hpp"

unsigned int hse_value = 25'000'000;

int main()
{
#if defined(CORE_CM4)
    hal::WaitForCortexM7();
#endif

    HAL_Init();
    ConfigureDefaultClockEvalH757I();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::EvalH757Ui ui;
#if defined(CORE_CM4)
    static services::DebugLed debugLed(ui.ledOrange);
#else
    static services::DebugLed debugLed(ui.ledGreen);
#endif

#if defined(CORE_CM7)
    hal::ReleaseCortexM4();
#endif

    eventInfrastructure.Run();
    __builtin_unreachable();
}
