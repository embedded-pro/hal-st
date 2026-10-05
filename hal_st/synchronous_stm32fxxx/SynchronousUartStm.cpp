#include "hal_st/synchronous_stm32fxxx/SynchronousUartStm.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"

namespace hal
{
    SynchronousUartStm::SynchronousUartStm(infra::ByteRange readBuffer, uint8_t aUartIndex, GpioPinStm& uartTx, GpioPinStm& uartRx, TimeKeeper& timeKeeper, uint32_t baudrate)
        : SynchronousUartStm(readBuffer, aUartIndex, uartTx, uartRx, uartTx, uartRx, timeKeeper, HwFlowControl::hwControlDisable, baudrate)
    {}

    SynchronousUartStm::SynchronousUartStm(infra::ByteRange readBuffer, uint8_t aUartIndex, GpioPinStm& uartTx, GpioPinStm& uartRx, GpioPinStm& uartRts, GpioPinStm& uartCts, TimeKeeper& timeKeeper,
        HwFlowControl flowControl, uint32_t baudrate)
        : uartIndex(aUartIndex - 1)
        , uartTx(uartTx, PinConfigTypeStm::uartTx, aUartIndex)
        , uartRx(uartRx, PinConfigTypeStm::uartRx, aUartIndex)
        , timeKeeper(timeKeeper)
        , readBuffer(readBuffer)
        , contentsBegin(readBuffer.begin())
        , contentsEnd(readBuffer.begin())
    {
        if (flowControl != HwFlowControl::hwControlDisable)
        {
            this->uartRts.emplace(uartRts, PinConfigTypeStm::uartRts, aUartIndex);
            this->uartCts.emplace(uartCts, PinConfigTypeStm::uartCts, aUartIndex);
        }
        Register(peripheralUartIrq[uartIndex]);
        EnableClockUart(uartIndex);

        UART_HandleTypeDef uartHandle = {};

        uartHandle.Instance = peripheralUart[uartIndex];
        uartHandle.Init.BaudRate = baudrate;
        uartHandle.Init.WordLength = USART_WORDLENGTH_8B;
        uartHandle.Init.StopBits = USART_STOPBITS_1;
        uartHandle.Init.Parity = USART_PARITY_NONE;
        uartHandle.Init.Mode = USART_MODE_TX_RX;
        uartHandle.Init.HwFlowCtl = flowControl;
#if defined(UART_ONE_BIT_SAMPLE_ENABLE)
        uartHandle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_ENABLE;
#endif
        uartHandle.Init.OverSampling = UART_OVERSAMPLING_8;
#if defined(UART_ADVFEATURE_SWAP_INIT)
        uartHandle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
        uartHandle.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_DISABLE;
#endif

        HAL_UART_Init(&uartHandle);

        peripheralUart[uartIndex]->CR2 &= ~USART_CLOCK_ENABLED;
        peripheralUart[uartIndex]->CR1 |= USART_CR1_RXNEIE;
    }

    SynchronousUartStm::~SynchronousUartStm()
    {
        peripheralUart[uartIndex]->CR1 &= ~(USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE);

        UART_HandleTypeDef uartHandle = {};
        uartHandle.Instance = peripheralUart[uartIndex];
        HAL_UART_DeInit(&uartHandle);
        DisableClockUart(uartIndex);
    }

    void SynchronousUartStm::SendData(infra::ConstByteRange data)
    {
        for (uint8_t byte : data)
        {
#if defined(USART_ISR_TXE)
            while ((peripheralUart[uartIndex]->ISR & USART_ISR_TXE) == 0)
            {}

            peripheralUart[uartIndex]->TDR = byte;
#else
            while ((peripheralUart[uartIndex]->SR & USART_SR_TXE) == 0)
            {}

            peripheralUart[uartIndex]->DR = byte;
#endif
        }

#if defined(USART_ISR_TXE)
        while ((peripheralUart[uartIndex]->ISR & USART_ISR_TXE) == 0)
        {}
#else
        while ((peripheralUart[uartIndex]->SR & USART_SR_TXE) == 0)
        {}
#endif
    }

    bool SynchronousUartStm::ReceiveData(infra::ByteRange data)
    {
        timeKeeper.Reset();

        for (uint8_t& byte : data)
        {
            while (Empty())
            {
                if (timeKeeper.Timeout())
                    return false;
            }

            byte = *contentsBegin;

            if (contentsBegin == readBuffer.end() - 1)
                contentsBegin = readBuffer.begin();
            else
                ++contentsBegin;
        }

        return true;
    }

    void SynchronousUartStm::Invoke()
    {
#if defined(USART_ISR_RXNE)
        while (peripheralUart[uartIndex]->ISR & USART_ISR_RXNE)
#else
        while (peripheralUart[uartIndex]->SR & USART_SR_RXNE)
#endif
        {
#if defined(USART_RDR_RDR)
            uint8_t received = peripheralUart[uartIndex]->RDR;
#else
            uint8_t received = peripheralUart[uartIndex]->DR;
#endif
            if (!Full())
            {
                *contentsEnd = received;

                if (contentsEnd == readBuffer.end() - 1)
                    contentsEnd = readBuffer.begin();
                else
                    ++contentsEnd;
            }
        }

#if defined(USART_ICR_ORECF)
        if (peripheralUart[uartIndex]->ISR & USART_ISR_ORE)
            peripheralUart[uartIndex]->ICR = USART_ICR_ORECF;
#else
        // An SR then DR read clears ORE; while RXNE is set the receive loop does that read and keeps the byte
        if ((peripheralUart[uartIndex]->SR & (USART_SR_ORE | USART_SR_RXNE)) == USART_SR_ORE)
            static_cast<void>(peripheralUart[uartIndex]->DR);
#endif
    }

    bool SynchronousUartStm::Full() const
    {
        return (contentsEnd == readBuffer.end() - 1 || contentsBegin == contentsEnd + 1) && (contentsEnd != readBuffer.end() - 1 || contentsBegin == readBuffer.begin());
    }

    bool SynchronousUartStm::Empty() const
    {
        return contentsBegin.load() == contentsEnd.load();
    }

    SynchronousUartStmSendOnly::SynchronousUartStmSendOnly(uint8_t aUartIndex, GpioPinStm& uartTx, uint32_t baudrate)
        : SynchronousUartStmSendOnly(aUartIndex, uartTx, uartTx, HwFlowControl::hwControlDisable, baudrate)
    {}

    SynchronousUartStmSendOnly::SynchronousUartStmSendOnly(uint8_t aUartIndex, GpioPinStm& uartTx, GpioPinStm& uartRts,
        HwFlowControl flowControl, uint32_t baudrate)
        : uartBase(peripheralUart[aUartIndex - 1])
        , uartTx(uartTx, PinConfigTypeStm::uartTx, aUartIndex)
    {
        EnableClockUart(aUartIndex - 1);

        if (flowControl != HwFlowControl::hwControlDisable)
            this->uartRts.emplace(uartRts, PinConfigTypeStm::uartRts, aUartIndex);

        UartStmHalInit(flowControl, baudrate);
    }

#if defined(STM32WB)
    SynchronousUartStmSendOnly::SynchronousUartStmSendOnly(uint8_t aUartIndex, GpioPinStm& uartTx, SyncLpUart lpUart, uint32_t baudrate)
        : SynchronousUartStmSendOnly(aUartIndex, uartTx, uartTx, lpUart, HwFlowControl::hwControlDisable, baudrate)
    {}

    SynchronousUartStmSendOnly::SynchronousUartStmSendOnly(uint8_t aUartIndex, GpioPinStm& uartTx, GpioPinStm& uartRts, SyncLpUart lpUart, HwFlowControl flowControl, uint32_t baudrate)
        : uartBase(peripheralLpuart[aUartIndex - 1])
        , uartTx(uartTx, PinConfigTypeStm::lpuartTx, aUartIndex)
    {
        EnableClockLpuart(aUartIndex - 1);

        if (flowControl != HwFlowControl::hwControlDisable)
            this->uartRts.emplace(uartRts, PinConfigTypeStm::lpuartRts, aUartIndex);

        UartStmHalInit(flowControl, baudrate);
    }
#endif

    SynchronousUartStmSendOnly::~SynchronousUartStmSendOnly()
    {
        uartBase->CR1 &= ~(USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE);

        UART_HandleTypeDef uartHandle = {};
        uartHandle.Instance = uartBase;
        HAL_UART_DeInit(&uartHandle);

#if defined(HAS_PERIPHERAL_LPUART)
        for (std::size_t i = 0; i != peripheralLpuart.size(); ++i)
            if (peripheralLpuart[i] == uartBase)
            {
                DisableClockLpuart(i);
                return;
            }
#endif

        for (std::size_t i = 0; i != peripheralUart.size(); ++i)
            if (peripheralUart[i] == uartBase)
                DisableClockUart(i);
    }

    void SynchronousUartStmSendOnly::SendData(infra::ConstByteRange data)
    {
        for (uint8_t byte : data)
        {
#if defined(USART_ISR_TXE)
            while ((uartBase->ISR & USART_ISR_TXE) == 0)
            {}

            uartBase->TDR = byte;
#else
            while ((uartBase->SR & USART_SR_TXE) == 0)
            {}

            uartBase->DR = byte;
#endif
        }

#if defined(USART_ISR_TXE)
        while ((uartBase->ISR & USART_ISR_TXE) == 0)
        {}
#else
        while ((uartBase->SR & USART_SR_TXE) == 0)
        {}
#endif
    }

    bool SynchronousUartStmSendOnly::ReceiveData(infra::ByteRange data)
    {
        return false;
    }

    void SynchronousUartStmSendOnly::UartStmHalInit(HwFlowControl flowControl, uint32_t baudrate)
    {
        UART_HandleTypeDef uartHandle = {};

        uartHandle.Instance = uartBase;
        uartHandle.Init.BaudRate = baudrate;
        uartHandle.Init.WordLength = USART_WORDLENGTH_8B;
        uartHandle.Init.StopBits = USART_STOPBITS_1;
        uartHandle.Init.Parity = USART_PARITY_NONE;
        uartHandle.Init.Mode = USART_MODE_TX_RX;
        uartHandle.Init.HwFlowCtl = flowControl;
#if defined(USART_OVERSAMPLING_8)
        uartHandle.Init.OverSampling = USART_OVERSAMPLING_8;
        uartHandle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_ENABLE;
#else
        uartHandle.Init.OverSampling = UART_OVERSAMPLING_8;
#endif
#if defined(UART_ADVFEATURE_SWAP_INIT)
        uartHandle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
        uartHandle.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_DISABLE;
#endif
        HAL_UART_Init(&uartHandle);

        uartBase->CR2 &= ~USART_CLOCK_ENABLED;

#if defined(STM32F4) || defined(STM32G0)
        uartBase->CR1 |= USART_IT_RXNE & USART_IT_MASK;
#else
        uartBase->CR1 |= 1 << (USART_IT_RXNE & USART_IT_MASK);
#endif
    }
}
