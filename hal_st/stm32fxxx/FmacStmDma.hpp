#pragma once

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/FmacStm.hpp"

#if defined(HAS_PERIPHERAL_FMAC)

namespace hal
{
    class FmacStmDma
        : public FmacBaseStm
    {
    public:
        FmacStmDma(DmaStm::TransmitStream& writeStream, DmaStm::ReceiveStream& readStream, const Config& config = Config());
        ~FmacStmDma();

        void Filter(infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> output, const infra::Function<void(bool saturated)>& onDone);

    private:
        void WriteDone();
        void ReadDone();

    private:
        TransmitDmaChannel write;
        ReceiveDmaChannel read;
        infra::AutoResetFunction<void(bool saturated)> onDone;
    };
}

#endif
