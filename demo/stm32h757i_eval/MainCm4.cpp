#include "demo/stm32h757i_eval/EvalUi.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "services/peripheral/DebugLed.hpp"

unsigned int hse_value = 25'000'000;

int main()
{
    hal::WaitForCortexM7();

    HAL_Init();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::EvalH757Ui ui;
    static services::DebugLed debugLed(ui.ledOrange);

    eventInfrastructure.Run();
    __builtin_unreachable();
}
