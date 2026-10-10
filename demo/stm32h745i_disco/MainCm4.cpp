#include "demo/stm32h745i_disco/DiscoveryH745Ui.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "services/peripheral/DebugLed.hpp"

unsigned int hse_value = 25'000'000;

int main()
{
    hal::WaitForCortexM7();

    HAL_Init();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::DiscoveryH745Ui ui;
    static services::DebugLed debugLed(ui.ledArduino);

    eventInfrastructure.Run();
    __builtin_unreachable();
}
