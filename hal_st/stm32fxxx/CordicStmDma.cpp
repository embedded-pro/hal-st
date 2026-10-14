#include "hal_st/stm32fxxx/CordicStmDma.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <limits>

#if defined(HAS_PERIPHERAL_CORDIC)

#if defined(STM32G4)
#include "stm32g4xx_ll_cordic.h"
#endif

namespace hal
{
    CordicStmDma::CordicStmDma(DmaStm::TransmitStream& writeStream, DmaStm::ReceiveStream& readStream, const Config& config)
        : CordicBaseStm(config)
        , write(writeStream, &Instance()->WDATA, sizeof(uint32_t), [this]()
              {
                  WriteDone();
              })
        , read(readStream, &Instance()->RDATA, sizeof(uint32_t), [this]()
              {
                  ReadDone();
              })
    {}

    CordicStmDma::~CordicStmDma()
    {
        LL_CORDIC_DisableDMAReq_WR(Instance());
        LL_CORDIC_DisableDMAReq_RD(Instance());
        write.StopTransfer();
        read.StopTransfer();
    }

    void CordicStmDma::Calculate(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results, const infra::Function<void()>& onDone)
    {
        Calculations(arguments, results);
        really_assert(arguments.size() * sizeof(int32_t) <= std::numeric_limits<uint16_t>::max());
        really_assert(results.size() * sizeof(int32_t) <= std::numeric_limits<uint16_t>::max());
        really_assert(this->onDone == nullptr);

        this->onDone = onDone;

        read.StartReceive(results);
        write.StartTransmit(arguments);
        LL_CORDIC_EnableDMAReq_RD(Instance());
        LL_CORDIC_EnableDMAReq_WR(Instance());
    }

    void CordicStmDma::WriteDone()
    {
        LL_CORDIC_DisableDMAReq_WR(Instance());
    }

    void CordicStmDma::ReadDone()
    {
        LL_CORDIC_DisableDMAReq_RD(Instance());
        onDone();
    }
}

#endif
