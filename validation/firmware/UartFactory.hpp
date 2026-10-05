#pragma once

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/UartStm.hpp"
#include "hal_st/stm32fxxx/UartStmDma.hpp"
#include "hal_st/stm32fxxx/UartStmDuplexDma.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousUartStm.hpp"
#include "services/hil/commands/HilUartCommands.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class UartFactoryStm
        : public services::HilUartFactory
    {
    public:
        UartFactoryStm(const services::HilPinNaming& naming, hal::DmaStm& dma);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, hal::TimeKeeper& timeKeeper, services::HilUartHandle& handle) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

    private:
        using FlowControl = hal::SynchronousUartStm::HwFlowControl;
        using DuplexUart = hal::UartStmDuplexDma::WithRxBuffer<64>;
        // The polled driver buffers everything until uart.recv drains it: one ring slot stays empty, uart.recv returns up to 256 bytes
        using SynchronousUart = hal::SynchronousUartStm::WithStorage<257>;
        using SendOnlyUart = hal::SynchronousUartStmSendOnly;

        struct Request
        {
            bool lpuart = false;
            std::optional<HilPinId> tx;
            std::optional<HilPinId> rx;
            std::optional<HilPinId> rts;
            std::optional<HilPinId> cts;
            uint32_t baud = 115200;
            uint32_t parity = USART_PARITY_NONE;
            FlowControl flow = FlowControl::hwControlDisable;
            bool swap = false;
            bool dma = false;
            bool duplex = false;
            bool synchronous = false;
            bool sendOnly = false;
        };

        struct ClaimedPins
        {
            hal::GpioPin* tx = nullptr;
            hal::GpioPin* rx = nullptr;
            hal::GpioPin* rts = nullptr;
            hal::GpioPin* cts = nullptr;
        };

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Parse(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(uint8_t index, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const;
        void Construct(uint8_t index, const Request& request, const ClaimedPins& claimed, hal::TimeKeeper& timeKeeper, services::HilUartHandle& handle);

    private:
        const services::HilPinNaming& naming;
        hal::DmaStm& dma;
        std::optional<hal::DmaStm::TransmitStream> transmitStream;
        std::optional<hal::DmaStm::ReceiveStream> receiveStream;
        std::variant<std::monostate, hal::UartStm, hal::UartStmDma, DuplexUart, SynchronousUart, SendOnlyUart> driver;
        hal::SerialCommunication* serial = nullptr;
    };
}
