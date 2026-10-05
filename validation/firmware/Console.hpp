#pragma once

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/UartStmDuplexDma.hpp"
#include "infra/stream/OutputStream.hpp"
#include "services/hil/HilResponse.hpp"
#include "services/hil/HilTerminal.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/Tracer.hpp"
#include <cstddef>

namespace validation
{
    struct Console
    {
        static constexpr std::size_t receiveBufferSize = 128;
        static constexpr std::size_t transmitBufferSize = 1024;
        static constexpr std::size_t queueSize = 256;

        static_assert(queueSize >= receiveBufferSize / 2, "terminal queue must absorb a whole DMA half-buffer");

        Console();

        hal::DmaStm dma;
        hal::GpioPinStm tx;
        hal::GpioPinStm rx;
        hal::DmaStm::TransmitStream transmitStream;
        hal::DmaStm::ReceiveStream receiveStream;
        hal::UartStmDuplexDma::WithRxBuffer<receiveBufferSize> uart;
        services::StreamWriterOnSerialCommunication::WithStorage<transmitBufferSize> writer{ uart };
        infra::TextOutputStream::WithErrorPolicy stream{ writer };
        services::TracerToStream tracer{ stream };
        services::HilResponse response{ tracer };
        services::HilTerminal::WithMaxQueueAndMaxHistory<queueSize, 1> terminal{ uart, tracer, response };
    };
}
