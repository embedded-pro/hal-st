#pragma once

#include "hal_st/instantiations/StmTracerInfrastructure.hpp"

namespace main_
{
    struct EvalH757TracerInfrastructure
    {
        hal::GpioPinStm traceUartTx{ hal::Port::B, 14 };
        hal::GpioPinStm traceUartRx{ hal::Port::B, 15 };

        main_::StmTracerInfrastructure tracerInfrastructure{ { 1, traceUartTx, traceUartRx } };
        services::Tracer& tracer{ tracerInfrastructure.tracer };
    };
}
