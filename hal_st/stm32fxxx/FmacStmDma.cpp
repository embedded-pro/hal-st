#include "hal_st/stm32fxxx/FmacStmDma.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <limits>

#if defined(HAS_PERIPHERAL_FMAC)

#if defined(STM32G4)
#include "stm32g4xx_ll_fmac.h"
#endif

namespace hal
{
    FmacStmDma::FmacStmDma(DmaStm::TransmitStream& writeStream, DmaStm::ReceiveStream& readStream, const Config& config)
        : FmacBaseStm(config)
        , write(writeStream, &Instance()->WDATA, sizeof(uint32_t), [this]()
              {
                  WriteDone();
              })
        , read(readStream, &Instance()->RDATA, sizeof(uint32_t), [this]()
              {
                  ReadDone();
              })
    {}

    FmacStmDma::~FmacStmDma()
    {
        LL_FMAC_DisableDMAReq_WRITE(Instance());
        LL_FMAC_DisableDMAReq_READ(Instance());
        write.StopTransfer();
        read.StopTransfer();
    }

    void FmacStmDma::Filter(infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> output, const infra::Function<void(bool saturated)>& onDone)
    {
        CheckConfigured();
        really_assert(input.size() == output.size() && !input.empty());
        really_assert(input.size() * sizeof(int16_t) <= std::numeric_limits<uint16_t>::max());
        really_assert(this->onDone == nullptr);

        this->onDone = onDone;

        read.StartReceive(output);
        write.StartTransmit(input);
        LL_FMAC_EnableDMAReq_READ(Instance());
        LL_FMAC_EnableDMAReq_WRITE(Instance());
    }

    void FmacStmDma::WriteDone()
    {
        LL_FMAC_DisableDMAReq_WRITE(Instance());
    }

    void FmacStmDma::ReadDone()
    {
        LL_FMAC_DisableDMAReq_READ(Instance());
        onDone(Saturated());
    }
}

#endif
