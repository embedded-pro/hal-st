#include "examples/stm32h757i_eval/DefaultClockEvalH757I.hpp"
#include "examples/stm32h757i_eval/EvalUi.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "services/peripheral/DebugLed.hpp"
#if defined(CORE_CM7)
#include "examples/stm32h757i_eval/EvalTracerInfrastructure.hpp"
#include "infra/timer/Timer.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <chrono>
#endif

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

#if defined(CORE_CM7)
    static services::DebugLed debugLed(ui.ledGreen);
    static main_::EvalH757TracerInfrastructure tracerInfrastructure;

    services::SetGlobalTracerInstance(tracerInfrastructure.tracer);

    hal::ReleaseCortexM4();

    static infra::TimerRepeating timerRepeating{ std::chrono::seconds{ 1 }, []
        {
            services::GlobalTracer().Trace() << "Hello World !";
        } };
#else
    static services::DebugLed debugLed(ui.ledOrange);
#endif

    eventInfrastructure.Run();
    __builtin_unreachable();
}
