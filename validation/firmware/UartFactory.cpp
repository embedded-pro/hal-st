#include "validation/firmware/UartFactory.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;
        using FlowControl = hal::SynchronousUartStm::HwFlowControl;

        constexpr std::array<const char*, 13> openKeys{ { "lp", "tx", "rx", "rts", "cts", "baud", "parity", "flow", "swap", "dma", "duplex", "sync", "sendonly" } };

        constexpr std::array<HilChoice<uint32_t>, 3> parities{ {
            { "none", USART_PARITY_NONE },
            { "even", USART_PARITY_EVEN },
            { "odd", USART_PARITY_ODD },
        } };

        constexpr std::array<HilChoice<FlowControl>, 4> flowControls{ {
            { "none", FlowControl::hwControlDisable },
            { "rts", FlowControl::hwControlRtsEnable },
            { "cts", FlowControl::hwControlCtsEnable },
            { "rtscts", FlowControl::hwControlRtsCtsEnable },
        } };

        constexpr uint32_t minimumBaudRate = 300;
        constexpr uint32_t maximumBaudRate = 12000000;

        // The HAL keeps UART_BRR_MIN and LPUART_BRR_MIN private to *_hal_uart.c
        constexpr uint32_t usartMinimumDivider = 0x10;
        constexpr uint32_t lpuartMinimumDivider = 0x300;

        // SynchronousUartStmSendOnly has SyncLpUart constructors on STM32WB only
#if defined(STM32WB)
        constexpr bool lpuartSendOnly = true;
#else
        constexpr bool lpuartSendOnly = false;
#endif

        struct PinFunctions
        {
            hal::PinConfigTypeStm tx;
            hal::PinConfigTypeStm rx;
            hal::PinConfigTypeStm rts;
            hal::PinConfigTypeStm cts;
        };

        constexpr PinFunctions usartFunctions{ hal::PinConfigTypeStm::uartTx, hal::PinConfigTypeStm::uartRx, hal::PinConfigTypeStm::uartRts, hal::PinConfigTypeStm::uartCts };
        constexpr PinFunctions lpuartFunctions{ hal::PinConfigTypeStm::lpuartTx, hal::PinConfigTypeStm::lpuartRx, hal::PinConfigTypeStm::lpuartRts, hal::PinConfigTypeStm::lpuartCts };

        struct DriverPins
        {
            hal::GpioPinStm& tx;
            hal::GpioPinStm& rx;
            hal::GpioPinStm& rts;
            hal::GpioPinStm& cts;
        };

        const PinFunctions& FunctionsOf(bool lpuart)
        {
            return lpuart ? lpuartFunctions : usartFunctions;
        }

        bool UsesRts(FlowControl flow)
        {
            return flow == FlowControl::hwControlRtsEnable || flow == FlowControl::hwControlRtsCtsEnable;
        }

        bool UsesCts(FlowControl flow)
        {
            return flow == FlowControl::hwControlCtsEnable || flow == FlowControl::hwControlRtsCtsEnable;
        }

        bool InstanceExists(uint8_t index, bool lpuart)
        {
            const auto& table = lpuart ? hal::peripheralLpuart : hal::peripheralUart;
            return index >= 1 && index <= table.size() && table[index - 1] != nullptr;
        }

        bool Selects(const UartPins& pins, uint8_t index, bool lpuart)
        {
            return pins.index == index && pins.lpuart == lpuart;
        }

        bool IsTerminal(uint8_t index, bool lpuart)
        {
            return Selects(board::terminal, index, lpuart);
        }

        // The terminal defaults to its own pins, so a bare uart.open of it passes every argument check and answers busy
        std::optional<UartPins> DefaultPins(uint8_t index, bool lpuart)
        {
            if (IsTerminal(index, lpuart))
                return board::terminal;

            if (board::defaultUart && Selects(*board::defaultUart, index, lpuart))
                return board::defaultUart;

            return std::nullopt;
        }

        bool BaudRateFits(uint8_t index, bool lpuart, uint32_t baud)
        {
            if (!IS_UART_BAUDRATE(baud))
                return false;

            const uint32_t clock = board::UartKernelClock(index, lpuart);

            if (lpuart)
            {
                const uint32_t divider = UART_DIV_LPUART(clock, baud, UART_PRESCALER_DIV1);
                return divider >= lpuartMinimumDivider && divider <= static_cast<uint32_t>(USART_BRR_LPUART);
            }

            const uint32_t divider = UART_DIV_SAMPLING8(clock, baud, UART_PRESCALER_DIV1);
            return divider >= usartMinimumDivider && divider <= static_cast<uint32_t>(USART_BRR_BRR);
        }

        bool Supports(const std::optional<HilPinId>& pin, hal::PinConfigTypeStm function, uint8_t index)
        {
            return !pin || SupportsFunction(*pin, function, index);
        }

        bool PinsSupportFunctions(uint8_t index, const PinFunctions& functions, HilPinId tx, const std::optional<HilPinId>& rx, const std::optional<HilPinId>& rts, const std::optional<HilPinId>& cts)
        {
            return SupportsFunction(tx, functions.tx, index) && Supports(rx, functions.rx, index) && Supports(rts, functions.rts, index) && Supports(cts, functions.cts, index);
        }

        template<class Driver, class Variant, class... Streams>
        Driver& EmplaceInterruptReceiving(Variant& driver, uint8_t index, bool lpuart, bool handshake, const DriverPins& pins, const hal::UartStm::Config& config, Streams&... streams)
        {
            if (lpuart)
            {
                if (handshake)
                    return driver.template emplace<Driver>(streams..., index, pins.tx, pins.rx, pins.rts, pins.cts, hal::LpUart{}, config);

                return driver.template emplace<Driver>(streams..., index, pins.tx, pins.rx, hal::LpUart{}, config);
            }

            if (handshake)
                return driver.template emplace<Driver>(streams..., index, pins.tx, pins.rx, pins.rts, pins.cts, config);

            return driver.template emplace<Driver>(streams..., index, pins.tx, pins.rx, config);
        }

        template<class Driver, class Variant>
        Driver& EmplaceSendOnly(Variant& driver, uint8_t index, bool lpuart, bool rts, const DriverPins& pins, uint32_t baud)
        {
            constexpr auto rtsOnly = Driver::HwFlowControl::hwControlRtsEnable;

#if defined(STM32WB)
            if (lpuart)
            {
                if (rts)
                    return driver.template emplace<Driver>(index, pins.tx, pins.rts, hal::SyncLpUart{}, rtsOnly, baud);

                return driver.template emplace<Driver>(index, pins.tx, hal::SyncLpUart{}, baud);
            }
#endif

            if (rts)
                return driver.template emplace<Driver>(index, pins.tx, pins.rts, rtsOnly, baud);

            return driver.template emplace<Driver>(index, pins.tx, baud);
        }
    }

    UartFactoryStm::UartFactoryStm(const services::HilPinNaming& naming, hal::DmaStm& dma)
        : naming(naming)
        , dma(dma)
    {}

    uint8_t UartFactoryStm::Instances() const
    {
        return board::uartInstances;
    }

    infra::MemoryRange<const char* const> UartFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus UartFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus UartFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, hal::TimeKeeper& timeKeeper, services::HilUartHandle& handle)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        ClaimedPins claimed;
        status = Claim(index, request, pins, claimed);
        if (status != HilStatus::done)
            return status;

        Construct(index, request, claimed, timeKeeper, handle);
        return HilStatus::done;
    }

    void UartFactoryStm::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        if (serial != nullptr)
            serial->ReceiveData(nullptr);

        serial = nullptr;
        driver.emplace<std::monostate>();
        receiveStream.reset();
        transmitStream.reset();
        onClosed();
    }

    HilStatus UartFactoryStm::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        const HilStatus status = Parse(arguments, request);
        if (status != HilStatus::done)
            return status;

        if (!InstanceExists(index, request.lpuart))
            return HilStatus::range;

        if (request.dma + request.duplex + request.synchronous + request.sendOnly > 1)
            return HilStatus::usage;

        if (UsesRts(request.flow) != request.rts.has_value() || UsesCts(request.flow) != request.cts.has_value())
            return HilStatus::usage;

        if (request.lpuart && (request.duplex || request.synchronous))
            return HilStatus::unsupported;

        const bool polled = request.synchronous || request.sendOnly;
        if (polled && (request.parity != USART_PARITY_NONE || request.swap))
            return HilStatus::unsupported;

        if (request.sendOnly && ((request.flow != FlowControl::hwControlDisable && request.flow != FlowControl::hwControlRtsEnable) || (request.lpuart && !lpuartSendOnly)))
            return HilStatus::unsupported;

        if (!polled && request.flow != FlowControl::hwControlDisable && request.flow != FlowControl::hwControlRtsCtsEnable)
            return HilStatus::unsupported;

        if ((request.dma || request.duplex) && !board::UartDma(index, request.lpuart))
            return HilStatus::unsupported;

        if (!BaudRateFits(index, request.lpuart, request.baud))
            return HilStatus::range;

        const auto defaults = DefaultPins(index, request.lpuart);
        if (defaults && !request.tx && !request.rx && !request.rts && !request.cts)
        {
            request.tx = defaults->tx;
            request.rx = defaults->rx;
        }

        if (!request.tx || (!request.rx && !request.sendOnly))
            return HilStatus::usage;

        if (!PinsSupportFunctions(index, FunctionsOf(request.lpuart), *request.tx, request.rx, request.rts, request.cts))
            return HilStatus::pin;

        if (IsTerminal(index, request.lpuart))
            return HilStatus::busy;

        return HilStatus::done;
    }

    HilStatus UartFactoryStm::Parse(const services::HilArguments& arguments, Request& request) const
    {
        HilStatus status = HilStatus::done;
        arguments.Flag("lp", request.lpuart, status);
        arguments.Pin("tx", naming, request.tx, status);
        arguments.Pin("rx", naming, request.rx, status);
        arguments.Pin("rts", naming, request.rts, status);
        arguments.Pin("cts", naming, request.cts, status);
        arguments.Number("baud", request.baud, minimumBaudRate, maximumBaudRate, status);
        arguments.Select("parity", request.parity, parities, status);
        arguments.Select("flow", request.flow, flowControls, status);
        arguments.Flag("swap", request.swap, status);
        arguments.Flag("dma", request.dma, status);
        arguments.Flag("duplex", request.duplex, status);
        arguments.Flag("sync", request.synchronous, status);
        arguments.Flag("sendonly", request.sendOnly, status);
        return status;
    }

    HilStatus UartFactoryStm::Claim(uint8_t index, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const
    {
        const auto& functions = FunctionsOf(request.lpuart);

        HilStatus status = pins.ClaimFunction(request.tx, Function(functions.tx), index, claimed.tx);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.rx, Function(functions.rx), index, claimed.rx);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.rts, Function(functions.rts), index, claimed.rts);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.cts, Function(functions.cts), index, claimed.cts);

        return status;
    }

    void UartFactoryStm::Construct(uint8_t index, const Request& request, const ClaimedPins& claimed, hal::TimeKeeper& timeKeeper, services::HilUartHandle& handle)
    {
        const DriverPins pins{ PinOrDummy(claimed.tx), PinOrDummy(claimed.rx), PinOrDummy(claimed.rts), PinOrDummy(claimed.cts) };
        const bool handshake = request.flow != FlowControl::hwControlDisable;
        handle.baudRate = request.baud;

        if (request.sendOnly)
        {
            handle.synchronous = &EmplaceSendOnly<SendOnlyUart>(driver, index, request.lpuart, handshake, pins, request.baud);
            return;
        }

        if (request.synchronous)
        {
            if (handshake)
                handle.synchronous = &driver.emplace<SynchronousUart>(index, pins.tx, pins.rx, pins.rts, pins.cts, timeKeeper, request.flow, request.baud);
            else
                handle.synchronous = &driver.emplace<SynchronousUart>(index, pins.tx, pins.rx, timeKeeper, request.baud);

            return;
        }

        const hal::UartStm::Config config{ request.baud, request.parity, hal::cortex::InterruptPriority::normal, request.swap };

        if (request.dma || request.duplex)
        {
            const DmaRequests requests = *board::UartDma(index, request.lpuart);
            transmitStream.emplace(dma, hal::DmaChannelId(1, board::uartDmaChannel, requests.transmit));

            if (request.duplex)
                receiveStream.emplace(dma, hal::DmaChannelId(1, static_cast<uint8_t>(board::uartDmaChannel + 1), requests.receive));
        }

        if (request.duplex)
        {
            if (handshake)
                serial = &driver.emplace<DuplexUart>(*transmitStream, *receiveStream, index, pins.tx, pins.rx, pins.rts, pins.cts, config);
            else
                serial = &driver.emplace<DuplexUart>(*transmitStream, *receiveStream, index, pins.tx, pins.rx, config);
        }
        else if (request.dma)
            serial = &EmplaceInterruptReceiving<hal::UartStmDma>(driver, index, request.lpuart, handshake, pins, config, *transmitStream);
        else
            serial = &EmplaceInterruptReceiving<hal::UartStm>(driver, index, request.lpuart, handshake, pins, config);

        handle.serial = serial;
    }
}
