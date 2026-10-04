#include "validation/firmware/Console.hpp"
#include "BoardProfile.hpp"

namespace validation
{
    namespace
    {
        const hal::UartStmDuplexDma::Config terminalConfig{ board::terminalBaudRate, USART_PARITY_NONE, hal::cortex::InterruptPriority::low };
    }

    Console::Console()
        : tx(PortOf(board::terminal.tx), board::terminal.tx.index)
        , rx(PortOf(board::terminal.rx), board::terminal.rx.index)
        , transmitStream(dma, hal::DmaChannelId{ 1, 1, board::terminalDma.transmit })
        , receiveStream(dma, hal::DmaChannelId{ 1, 2, board::terminalDma.receive })
        , uart(transmitStream, receiveStream, board::terminal.index, tx, rx, terminalConfig)
    {}
}
