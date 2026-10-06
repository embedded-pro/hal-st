#include "examples/stm32h757i_eval/DefaultClockEvalH757I.hpp"
#include "examples/stm32h757i_eval/EvalTracerInfrastructure.hpp"
#include "examples/stm32h757i_eval/EvalUi.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "infra/timer/Timer.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <chrono>

unsigned int hse_value = 25'000'000;

int main()
{
    const bool cortexM4Stopped = hal::WaitForCortexM4Stop();

    HAL_Init();
    ConfigureDefaultClockEvalH757I();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::EvalH757Ui ui;
    static services::DebugLed debugLed(ui.ledGreen);
    static main_::EvalH757TracerInfrastructure tracerInfrastructure;

    services::SetGlobalTracerInstance(tracerInfrastructure.tracer);

    if (cortexM4Stopped)
        hal::ReleaseCortexM4();
    else
        tracerInfrastructure.tracer.Trace() << "Cortex-M4 did not enter stop mode, not released";

    static infra::TimerRepeating timerRepeating{ std::chrono::seconds{ 1 }, []
        {
            services::GlobalTracer().Trace() << "Hello World !";
        } };

    eventInfrastructure.Run();
    __builtin_unreachable();
}
