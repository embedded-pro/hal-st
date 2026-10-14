#include "hal_st/stm32fxxx/FmacStmDma.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <limits>

#if defined(HAS_PERIPHERAL_FMAC)

namespace hal
{
    FmacStmDma::FmacStmDma(DmaStm::TransmitStream& writeStream, DmaStm::ReceiveStream& readStream, const Config& config)
        : FmacBaseStm(config, FMAC_BUFFER_ACCESS_DMA)
        , write(writeStream, &Instance()->WDATA, sizeof(uint32_t), []() {})
        , read(readStream, &Instance()->RDATA, sizeof(uint32_t), [this]()
              {
                  ReadDone();
              })
    {}

    FmacStmDma::~FmacStmDma()
    {
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
    }

    void FmacStmDma::ReadDone()
    {
        onDone(Saturated());
    }
}

#endif
