#pragma once

#include "hal_st/stm32fxxx/CordicStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"

#if defined(HAS_PERIPHERAL_CORDIC)

namespace hal
{
    class CordicStmDma
        : public CordicBaseStm
    {
    public:
        CordicStmDma(DmaStm::TransmitStream& writeStream, DmaStm::ReceiveStream& readStream, const Config& config = Config());
        ~CordicStmDma();

        void Calculate(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results, const infra::Function<void()>& onDone);

    private:
        void WriteDone();
        void ReadDone();

    private:
        TransmitDmaChannel write;
        ReceiveDmaChannel read;
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
