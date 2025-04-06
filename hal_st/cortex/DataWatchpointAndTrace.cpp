#include "hal_st/cortex/DataWatchpointAndTrace.hpp"

namespace hal
{
    DataWatchPointAndTrace::DataWatchPointAndTrace()
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
    }

    DataWatchPointAndTrace::~DataWatchPointAndTrace()
    {
        CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;
    }

    void DataWatchPointAndTrace::Start() const
    {
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCEVTENA_Msk;
    }

    uint32_t DataWatchPointAndTrace::Stop() const
    {
        uint32_t cycles = DWT->CYCCNT;
        DWT->CTRL &= ~DWT_CTRL_CYCEVTENA_Msk;
        return cycles;
    }
}
