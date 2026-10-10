#include "validation/firmware/QuadSpiGroup.hpp"

#if defined(HAS_PERIPHERAL_QUADSPI)

#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <chrono>
#include <cstddef>
#include <limits>
#include DEVICE_HEADER
#if defined(STM32G4)
#include "stm32g4xx_ll_dma.h"
#endif

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;

        constexpr uint8_t quadSpiIndex = 1;
        constexpr uint8_t pinFunctionInstance = 0;
        constexpr uint32_t maximumPrescaler = 255;
        constexpr uint32_t maximumFlashSizeLog2 = 32;
        constexpr uint32_t maximumDummyCycles = 31;
        constexpr uint32_t maximumPhaseBytes = 4;
        constexpr uint32_t maximumRepeat = 8;
        constexpr infra::Duration operationTimeout = std::chrono::milliseconds(2000);

        constexpr std::array<const char*, 3> openKeys{ { "variant", "prescaler", "size" } };
        constexpr std::array<const char*, 14> commandKeys{ { "instr", "addr", "abytes", "alt", "altbytes", "dummy", "lines", "tx", "len", "pattern", "seed", "rx", "out", "repeat" } };
        constexpr std::array<const char*, 10> pollKeys{ { "instr", "addr", "abytes", "alt", "altbytes", "dummy", "lines", "match", "mask", "size" } };
        constexpr std::array<const char*, 5> transferKeys{ { "len", "pattern", "seed", "rx", "repeat" } };

        constexpr std::array<HilChoice<QuadSpiVariant>, 3> variantChoices{ {
            { "poll", QuadSpiVariant::poll },
            { "dma", QuadSpiVariant::dma },
            { "spi", QuadSpiVariant::spi },
        } };

        constexpr std::array<HilPinId, 6> pinIds{ { board::qspiPins.clk, board::qspiPins.ncs, board::qspiPins.io[0], board::qspiPins.io[1], board::qspiPins.io[2], board::qspiPins.io[3] } };
        constexpr std::array<hal::PinConfigTypeStm, 6> pinFunctions{ {
            hal::PinConfigTypeStm::quadSpiClock,
            hal::PinConfigTypeStm::quadSpiSlaveSelect,
            hal::PinConfigTypeStm::quadSpiData0,
            hal::PinConfigTypeStm::quadSpiData1,
            hal::PinConfigTypeStm::quadSpiData2,
            hal::PinConfigTypeStm::quadSpiData3,
        } };

#if defined(STM32G4)
        static_assert(board::qspiDma.dma == 2 && LL_DMA_CHANNEL_1 == 0 && LL_DMA_CHANNEL_7 == 6);

        constexpr uint32_t LlDmaChannel(uint8_t channel)
        {
            return channel - 1;
        }

        uint32_t QuadSpiKernelClock()
        {
            return HAL_RCC_GetHCLKFreq();
        }

        void ResetQuadSpi()
        {
            __HAL_RCC_QSPI_FORCE_RESET();
            __HAL_RCC_QSPI_RELEASE_RESET();
        }
#else
        static_assert(board::qspiDma.dma == 2 && LL_DMA_CHANNEL_1 == 1 && LL_DMA_CHANNEL_7 == 7);

        constexpr uint32_t LlDmaChannel(uint8_t channel)
        {
            return channel;
        }

        uint32_t QuadSpiKernelClock()
        {
            return HAL_RCC_GetHCLK4Freq();
        }

        void ResetQuadSpi()
        {
            __HAL_RCC_QUADSPI_FORCE_RESET();
            __HAL_RCC_QUADSPI_RELEASE_RESET();
        }
#endif

        bool Fits(uint32_t value, uint32_t bytes)
        {
            return bytes >= maximumPhaseBytes || value < (uint32_t{ 1 } << (8 * bytes));
        }

        uint32_t FifoLevel()
        {
            return READ_BIT(QUADSPI->SR, QUADSPI_SR_FLEVEL) >> QUADSPI_SR_FLEVEL_Pos;
        }

        bool PhasesShape(const services::HilArguments& arguments)
        {
            return (arguments.Has("addr") || !arguments.Has("abytes")) && (arguments.Has("alt") || !arguments.Has("altbytes"));
        }

        bool CommandShape(const services::HilArguments& arguments)
        {
            const bool transmit = arguments.Has("tx");
            const bool generated = arguments.Has("len");
            const bool receive = arguments.Has("rx");
            const bool patterned = arguments.Has("pattern") || arguments.Has("seed");
            const int dataPhases = static_cast<int>(transmit) + static_cast<int>(generated) + static_cast<int>(receive);

            return dataPhases <= 1 && (generated || !patterned) && (receive || !arguments.Has("out")) && !(receive && arguments.Has("repeat")) && PhasesShape(arguments);
        }

        bool TransferShape(const services::HilArguments& arguments)
        {
            const bool transmit = arguments.Positional(1) != "-" || arguments.Has("len");
            const bool receive = arguments.Has("rx");

            return transmit != receive && !(receive && arguments.Has("repeat"));
        }

        template<class Phases>
        HilStatus ParsePhases(const services::HilArguments& arguments, Phases& phases)
        {
            uint32_t instruction = 0;
            uint32_t address = 0;
            uint32_t addressBytes = 3;
            uint32_t alternate = 0;
            uint32_t alternateBytes = 1;
            uint32_t dummyCycles = 0;
            uint32_t lines = 1;

            HilStatus status = HilStatus::done;
            arguments.Number("instr", instruction, 0, std::numeric_limits<uint8_t>::max(), status);
            arguments.Number("addr", address, 0, std::numeric_limits<uint32_t>::max(), status);
            arguments.Number("abytes", addressBytes, 1, maximumPhaseBytes, status);
            arguments.Number("alt", alternate, 0, std::numeric_limits<uint32_t>::max(), status);
            arguments.Number("altbytes", alternateBytes, 1, maximumPhaseBytes, status);
            arguments.Number("dummy", dummyCycles, 0, maximumDummyCycles, status);
            arguments.Number("lines", lines, 1, 4, status);
            if (status != HilStatus::done)
                return status;

            if ((lines != 1 && lines != 4) || !Fits(address, addressBytes) || !Fits(alternate, alternateBytes))
                return HilStatus::range;

            if (arguments.Has("instr"))
                phases.header.instruction = static_cast<uint8_t>(instruction);
            if (arguments.Has("addr"))
                phases.header.address = hal::QuadSpi::AddressToVector(address, static_cast<uint8_t>(addressBytes));
            if (arguments.Has("alt"))
                phases.header.alternate = hal::QuadSpi::AddressToVector(alternate, static_cast<uint8_t>(alternateBytes));
            phases.header.nofDummyCycles = static_cast<uint8_t>(dummyCycles);
            phases.lines = lines == 4 ? hal::QuadSpi::Lines::QuadSpeed() : hal::QuadSpi::Lines::SingleSpeed();
            return HilStatus::done;
        }
    }

    QuadSpiFactoryStm::QuadSpiFactoryStm(hal::DmaStm& dma, ResourceAllocation& resources)
        : dma(dma)
        , resources(resources)
    {}

    uint8_t QuadSpiFactoryStm::Instances() const
    {
        return quadSpiIndex + 1;
    }

    infra::MemoryRange<const char* const> QuadSpiFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus QuadSpiFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    void QuadSpiFactoryStm::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        this->onClosed = onClosed;

        // QuadSpiStm schedules its completion with a pointer to the driver, which must still be alive when it runs
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                Destroy();
            });
    }

    HilStatus QuadSpiFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins)
    {
        Request request;
        ClaimedPins claimed{};

        HilStatus status = Evaluate(index, arguments, request);
        if (status == HilStatus::done)
            status = Claim(request, pins, claimed);
        if (status == HilStatus::done)
            Construct(request, claimed);

        return status;
    }

    uint32_t QuadSpiFactoryStm::Clock() const
    {
        return clock;
    }

    hal::QuadSpi& QuadSpiFactoryStm::QuadSpi()
    {
        hal::QuadSpi* quadSpi = std::get_if<hal::QuadSpiStm>(&driver);
        if (quadSpi == nullptr)
            quadSpi = std::get_if<hal::QuadSpiStmDma>(&driver);

        really_assert(quadSpi != nullptr);
        return *quadSpi;
    }

    hal::SpiMaster* QuadSpiFactoryStm::Spi()
    {
        return spi ? &*spi : nullptr;
    }

    HilStatus QuadSpiFactoryStm::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        HilStatus status = HilStatus::done;
        arguments.Select("variant", request.variant, variantChoices, status);
        arguments.Number("prescaler", request.prescaler, 0, maximumPrescaler, status);
        arguments.Number("size", request.flashSizeLog2, 1, maximumFlashSizeLog2, status);
        if (status != HilStatus::done)
            return status;

        return index == quadSpiIndex ? HilStatus::done : HilStatus::range;
    }

    HilStatus QuadSpiFactoryStm::Claim(const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed)
    {
        for (std::size_t i = 0; i != pinIds.size(); ++i)
        {
            HilStatus status = pins.ClaimFunction(pinIds[i], Function(pinFunctions[i]), pinFunctionInstance, claimed[i]);
            if (status != HilStatus::done)
                return status;
        }

        if (request.variant == QuadSpiVariant::poll)
            return HilStatus::done;

        return resources.Claim(Resource::dma2, board::qspiDma.channel, owners::quadSpi);
    }

    void QuadSpiFactoryStm::Construct(const Request& request, const ClaimedPins& claimed)
    {
        clock = QuadSpiKernelClock() / (request.prescaler + 1);

        if (request.variant == QuadSpiVariant::poll)
        {
            hal::QuadSpiStm::Config config;
            config.prescaler = request.prescaler;
            config.flashSizeLog2 = request.flashSizeLog2;
            driver.emplace<hal::QuadSpiStm>(PinOrDummy(claimed[0]), PinOrDummy(claimed[1]), PinOrDummy(claimed[2]), PinOrDummy(claimed[3]), PinOrDummy(claimed[4]), PinOrDummy(claimed[5]), config);
            return;
        }

        hal::QuadSpiStmDma::Config config;
        config.prescaler = static_cast<uint8_t>(request.prescaler);
        config.flashSizeLog2 = request.flashSizeLog2;
        auto& transceiveStream = stream.emplace(dma, hal::DmaChannelId(board::qspiDma.dma, board::qspiDma.channel, board::qspiDmaRequest));
        auto& quadSpi = driver.emplace<hal::QuadSpiStmDma>(transceiveStream, PinOrDummy(claimed[0]), PinOrDummy(claimed[1]), PinOrDummy(claimed[2]), PinOrDummy(claimed[3]), PinOrDummy(claimed[4]), PinOrDummy(claimed[5]), config);

        if (request.variant == QuadSpiVariant::spi)
            spi.emplace(quadSpi);
    }

    void QuadSpiFactoryStm::Destroy()
    {
        spi.reset();

        // A command that timed out leaves its channel running, and HAL_DMA_Init keeps CCR.EN for the next open
        if (stream)
            LL_DMA_DisableChannel(DMA2, LlDmaChannel(board::qspiDma.channel));

        // A data-only indirect read hangs the QUADSPI with BUSY set (erratum): only an abort or a reset clears it
        ResetQuadSpi();

        driver.emplace<std::monostate>();
        stream.reset();
        resources.Release(Resource::dma2, board::qspiDma.channel, owners::quadSpi);
        onClosed();
    }

    QuadSpiGroup::QuadSpiGroup(services::HilContext& context, QuadSpiFactoryStm& factory)
        : services::HilSingleInstanceGroup(context, factory, owners::quadSpi)
        , factory(factory)
        , pending(context.response)
        , commands{ {
              OpenCommand("qspi.open", "<1> [variant=poll|dma|spi] [prescaler=] [size=]"),
              services::HilBind<QuadSpiGroup, &QuadSpiGroup::Execute>("qspi.cmd", "<1> [instr=] [addr=] [alt=] [dummy=] [lines=] [tx=|len=|rx=]", *this, context.response),
              services::HilBind<QuadSpiGroup, &QuadSpiGroup::Poll>("qspi.poll", "<1> match= mask= [size=] [instr=] [lines=]", *this, context.response),
              services::HilBind<QuadSpiGroup, &QuadSpiGroup::Transfer>("qspi.xfer", "<1> <txHex|-> [rx=]", *this, context.response),
              CloseCommand("qspi.close", "<1>"),
          } }
    {}

    infra::MemoryRange<const QuadSpiGroup::Command> QuadSpiGroup::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus QuadSpiGroup::OpenInstance(uint8_t index, const services::HilArguments& arguments)
    {
        return factory.Open(index, arguments, Pins());
    }

    void QuadSpiGroup::Opened(services::HilResponse::Line& line) const
    {
        line << " clk=" << factory.Clock();
    }

    void QuadSpiGroup::CloseInstance()
    {
        pending.Cancel();
        remaining = 0;
    }

    HilStatus QuadSpiGroup::Execute(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(commandKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        if (!CommandShape(arguments))
            return HilStatus::usage;

        Request next;
        status = ParsePhases(arguments, next.phases);
        if (status != HilStatus::done)
            return status;

        // The command in flight still reads or writes buffer, which the data arguments fill
        if (pending.Busy())
            return HilStatus::busy;

        status = ParseCommandData(arguments, next);
        if (status != HilStatus::done)
            return status;

        Start(next);
        return HilStatus::done;
    }

    HilStatus QuadSpiGroup::Poll(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(pollKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        if (!arguments.Has("match") || !arguments.Has("mask") || !PhasesShape(arguments))
            return HilStatus::usage;

        Request next;
        uint32_t match = 0;
        uint32_t mask = 0;
        uint32_t size = 1;
        status = ParsePhases(arguments, next.phases);
        arguments.Number("match", match, 0, std::numeric_limits<uint32_t>::max(), status);
        arguments.Number("mask", mask, 0, std::numeric_limits<uint32_t>::max(), status);
        arguments.Number("size", size, 1, maximumPhaseBytes, status);
        if (status != HilStatus::done)
            return status;

        if (!Fits(match, size) || !Fits(mask, size))
            return HilStatus::range;

        if (pending.Busy())
            return HilStatus::busy;

        request = next;
        const auto operation = current = pending.Start(operationTimeout);
        factory.QuadSpi().PollStatus(request.phases.header, static_cast<uint8_t>(size), match, mask, request.phases.lines, [this, operation]()
            {
                Polled(operation);
            });

        return HilStatus::done;
    }

    HilStatus QuadSpiGroup::Transfer(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, infra::MakeRange(transferKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        if (!TransferShape(arguments))
            return HilStatus::usage;

        if (factory.Spi() == nullptr)
            return HilStatus::unsupported;

        // The command in flight still reads or writes buffer, which the data arguments fill
        if (pending.Busy())
            return HilStatus::busy;

        Request next;
        status = ParseTransferData(arguments, next);
        if (status != HilStatus::done)
            return status;

        Start(next);
        return HilStatus::done;
    }

    HilStatus QuadSpiGroup::ParseCommandData(const services::HilArguments& arguments, Request& next)
    {
        HilStatus status = HilStatus::done;

        if (auto transmit = arguments.Key("tx"))
        {
            std::size_t size = 0;
            status = services::HilArguments::ParseHex(*transmit, infra::MakeRange(buffer), size);
            next.data = infra::Head(infra::MakeRange(buffer), size);
        }
        else if (arguments.Has("len"))
        {
            Pattern pattern = Pattern::inc;
            uint32_t seed = 0;
            uint32_t length = 0;
            arguments.Select("pattern", pattern, patternChoices, status);
            arguments.Number("seed", seed, 0, std::numeric_limits<uint32_t>::max(), status);
            arguments.Number("len", length, 1, static_cast<uint32_t>(buffer.size()), status);
            if (status == HilStatus::done)
            {
                next.data = infra::Head(infra::MakeRange(buffer), length);
                Generate(next.data, pattern, seed);
            }
        }
        else if (arguments.Has("rx"))
        {
            uint32_t length = 0;
            arguments.Number("rx", length, 1, static_cast<uint32_t>(buffer.size()), status);
            if (status == HilStatus::done)
                status = ParseOutput(arguments, next.output);
            if (status == HilStatus::done)
                status = CheckOutput(length, next.output);

            next.read = true;
            next.data = infra::Head(infra::MakeRange(buffer), length);
        }

        arguments.Number("repeat", next.repeat, 1, maximumRepeat, status);
        return status;
    }

    HilStatus QuadSpiGroup::ParseTransferData(const services::HilArguments& arguments, Request& next)
    {
        HilStatus status = ParsePayload(arguments, 1, infra::MakeRange(buffer), next.data);

        if (arguments.Has("rx"))
        {
            uint32_t length = 0;
            arguments.Number("rx", length, 1, static_cast<uint32_t>(maximumHexOutput), status);
            next.read = true;
            next.data = infra::Head(infra::MakeRange(buffer), length);
        }

        arguments.Number("repeat", next.repeat, 1, maximumRepeat, status);
        next.spi = true;
        return status;
    }

    void QuadSpiGroup::Start(const Request& next)
    {
        request = next;
        remaining = request.repeat;
        current = pending.Start(operationTimeout);
        Issue();
    }

    void QuadSpiGroup::Issue()
    {
        const auto operation = current;
        auto onWritten = [this, operation]()
        {
            Written(operation);
        };
        auto onReceived = [this, operation]()
        {
            Received(operation);
        };

        if (request.spi && request.read)
            factory.Spi()->SendAndReceive({}, request.data, hal::SpiAction::stop, onReceived);
        else if (request.spi)
            factory.Spi()->SendAndReceive(request.data, {}, hal::SpiAction::stop, onWritten);
        else if (request.read)
            factory.QuadSpi().ReceiveData(request.phases.header, request.data, request.phases.lines, onReceived);
        else
            factory.QuadSpi().SendData(request.phases.header, request.data, request.phases.lines, onWritten);
    }

    void QuadSpiGroup::Written(uint32_t operation)
    {
        const auto level = FifoLevel();

        if (!pending.Busy() || operation != current)
            return;

        if (--remaining != 0)
        {
            Issue();
            return;
        }

        if (pending.Complete(operation))
            Context().response.Ok() << " flevel=" << level;
    }

    void QuadSpiGroup::Received(uint32_t operation)
    {
        if (!pending.Complete(operation))
            return;

        auto line = Context().response.Ok();
        if (request.spi)
            (line << " rx=").Hex(request.data);
        else
            PrintData(line, request.data, request.output);
    }

    void QuadSpiGroup::Polled(uint32_t operation)
    {
        if (pending.Complete(operation))
            Context().response.Ok();
    }
}

#endif
