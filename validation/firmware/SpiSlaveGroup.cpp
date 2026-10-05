#include "validation/firmware/SpiSlaveGroup.hpp"
#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <algorithm>
#include <chrono>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint8_t instances = 4;
        constexpr uint32_t defaultWaitMs = 1000;
        constexpr uint32_t maximumWaitMs = 10000;

        constexpr std::array<const char*, 4> openKeys{ { "clk", "miso", "mosi", "nss" } };
        constexpr std::array<const char*, 4> armKeys{ { "rx", "len", "pattern", "seed" } };
        constexpr std::array<const char*, 2> resultKeys{ { "wait", "out" } };

        bool PinsSupportFunctions(uint8_t index, HilPinId clock, HilPinId miso, HilPinId mosi, HilPinId slaveSelect)
        {
            return SupportsFunction(clock, hal::PinConfigTypeStm::spiClock, index) && SupportsFunction(miso, hal::PinConfigTypeStm::spiMiso, index) && SupportsFunction(mosi, hal::PinConfigTypeStm::spiMosi, index) && SupportsFunction(slaveSelect, hal::PinConfigTypeStm::spiSlaveSelect, index);
        }

        Resource DmaResource(DmaChannel channel)
        {
            return channel.dma == 1 ? Resource::dma1 : Resource::dma2;
        }
    }

    SpiSlaveFactory::SpiSlaveFactory(const services::HilPinNaming& naming, hal::DmaStm& dma, ResourceAllocation& resources)
        : naming(naming)
        , dma(dma)
        , resources(resources)
    {}

    uint8_t SpiSlaveFactory::Instances() const
    {
        return instances;
    }

    infra::MemoryRange<const char* const> SpiSlaveFactory::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus SpiSlaveFactory::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    void SpiSlaveFactory::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        this->onClosed = onClosed;

        // Stops both DMA channels: a channel left enabled keeps its count for the next open on STM32WB
        slave->CancelTransmission();

        // A DMA completion dispatched before the cancel still runs against the driver
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                Destroy();
            });
    }

    HilStatus SpiSlaveFactory::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins)
    {
        Request request;
        ClaimedPins claimed;

        HilStatus status = Evaluate(index, arguments, request);
        if (status == HilStatus::done)
            status = Claim(index, request, pins, claimed);
        if (status != HilStatus::done)
            return status;

        const DmaRequests requests = *board::SpiDma(index);
        transmitStream.emplace(dma, hal::DmaChannelId(board::spiSlaveDma.transmit.dma, board::spiSlaveDma.transmit.channel, requests.transmit));
        receiveStream.emplace(dma, hal::DmaChannelId(board::spiSlaveDma.receive.dma, board::spiSlaveDma.receive.channel, requests.receive));
        slave.emplace(*transmitStream, *receiveStream, index, PinOrDummy(claimed.clock), PinOrDummy(claimed.miso), PinOrDummy(claimed.mosi), PinOrDummy(claimed.slaveSelect));
        return HilStatus::done;
    }

    hal::SpiSlave& SpiSlaveFactory::Slave()
    {
        really_assert(slave.has_value());
        return *slave;
    }

    HilStatus SpiSlaveFactory::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (!SpiExists(index))
            return HilStatus::range;

        HilStatus status = HilStatus::done;
        arguments.Pin("clk", naming, request.clock, status);
        arguments.Pin("miso", naming, request.miso, status);
        arguments.Pin("mosi", naming, request.mosi, status);
        arguments.Pin("nss", naming, request.slaveSelect, status);
        if (status != HilStatus::done)
            return status;

        if (!request.clock || !request.miso || !request.mosi || !request.slaveSelect)
            return HilStatus::usage;

        if (!PinsSupportFunctions(index, *request.clock, *request.miso, *request.mosi, *request.slaveSelect))
            return HilStatus::pin;

        if (!board::SpiDma(index))
            return HilStatus::unsupported;

        return HilStatus::done;
    }

    HilStatus SpiSlaveFactory::Claim(uint8_t index, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed)
    {
        HilStatus status = pins.ClaimFunction(request.clock, Function(hal::PinConfigTypeStm::spiClock), index, claimed.clock);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.miso, Function(hal::PinConfigTypeStm::spiMiso), index, claimed.miso);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.mosi, Function(hal::PinConfigTypeStm::spiMosi), index, claimed.mosi);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.slaveSelect, Function(hal::PinConfigTypeStm::spiSlaveSelect), index, claimed.slaveSelect);
        if (status != HilStatus::done)
            return status;

        this->index = index;
        status = resources.Claim(Resource::spi, index, owners::spiSlave);
        if (status == HilStatus::done)
            status = ClaimChannel(board::spiSlaveDma.transmit);
        if (status == HilStatus::done)
            status = ClaimChannel(board::spiSlaveDma.receive);
        if (status != HilStatus::done)
            ReleaseResources();

        return status;
    }

    HilStatus SpiSlaveFactory::ClaimChannel(DmaChannel channel)
    {
        return resources.Claim(DmaResource(channel), channel.channel, owners::spiSlave);
    }

    void SpiSlaveFactory::ReleaseResources()
    {
        resources.Release(Resource::spi, index, owners::spiSlave);
        resources.Release(DmaResource(board::spiSlaveDma.transmit), board::spiSlaveDma.transmit.channel, owners::spiSlave);
        resources.Release(DmaResource(board::spiSlaveDma.receive), board::spiSlaveDma.receive.channel, owners::spiSlave);
    }

    void SpiSlaveFactory::Destroy()
    {
        slave.reset();
        receiveStream.reset();
        transmitStream.reset();
        ReleaseResources();
        onClosed();
    }

    SpiSlaveCommands::SpiSlaveCommands(services::HilContext& context, SpiSlaveFactory& factory)
        : services::HilSingleInstanceGroup(context, factory, owners::spiSlave)
        , factory(factory)
        , commands{ {
              OpenCommand("spis.open", "<index> clk=<pin> miso=<pin> mosi=<pin> nss=<pin>"),
              services::HilBind<SpiSlaveCommands, &SpiSlaveCommands::Arm>("spis.arm", "<index> <txHex|-> [rx=] [len=] [pattern=] [seed=]", *this, context.response),
              services::HilBind<SpiSlaveCommands, &SpiSlaveCommands::Result>("spis.result", "<index> [wait=<ms>] [out=hex|crc]", *this, context.response),
              services::HilBind<SpiSlaveCommands, &SpiSlaveCommands::Cancel>("spis.cancel", "<index>", *this, context.response),
              CloseCommand("spis.close", "<index>"),
          } }
    {}

    infra::MemoryRange<const SpiSlaveCommands::Command> SpiSlaveCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus SpiSlaveCommands::OpenInstance(uint8_t index, const services::HilArguments& arguments)
    {
        armed = false;
        done = false;
        receiveSize = 0;
        return factory.Open(index, arguments, Pins());
    }

    void SpiSlaveCommands::CloseInstance()
    {
        StopWaiting();
        armed = false;
        done = false;
    }

    HilStatus SpiSlaveCommands::Arm(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, infra::MakeRange(armKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        // The armed transfer's transmit DMA still reads transmitBuffer
        if (armed)
            return HilStatus::busy;

        infra::ByteRange send;
        status = ParsePayload(arguments, 1, infra::MakeRange(transmitBuffer), send);
        auto receive = static_cast<uint32_t>(send.size());
        arguments.Number("rx", receive, 0, static_cast<uint32_t>(capacity), status);
        if (status != HilStatus::done)
            return status;

        // SpiSlaveStmDma runs full duplex on equal lengths only
        if ((send.empty() && receive == 0) || (!send.empty() && receive != 0 && receive != send.size()))
            return HilStatus::usage;

        armed = true;
        done = false;
        receiveSize = receive;
        std::fill(receiveBuffer.begin(), receiveBuffer.end(), 0);

        factory.Slave().SendAndReceive(send, infra::Head(infra::MakeRange(receiveBuffer), receiveSize), [this]()
            {
                TransferDone();
            });

        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus SpiSlaveCommands::Result(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(resultKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        auto requested = Output::hex;
        uint32_t wait = defaultWaitMs;
        status = ParseOutput(arguments, requested);
        arguments.Number("wait", wait, 0, maximumWaitMs, status);
        if (status == HilStatus::done)
            status = CheckOutput(receiveSize, requested);
        if (status != HilStatus::done)
            return status;

        if (waiting)
            return HilStatus::busy;

        output = requested;

        if (done)
            Report();
        else if (!armed || wait == 0)
            Context().response.Ok() << " done=0";
        else
        {
            // HilPendingOperation answers an expired wait with ERR timeout; a transfer still pending is a valid answer here
            waiting = true;
            waitTimer.Start(std::chrono::milliseconds(wait), [this]()
                {
                    waiting = false;
                    Context().response.Ok() << " done=0";
                });
        }

        return HilStatus::done;
    }

    HilStatus SpiSlaveCommands::Cancel(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        StopWaiting();
        const bool cancelled = factory.Slave().CancelTransmission();
        armed = false;
        done = false;

        Context().response.Ok() << " cancelled=" << static_cast<uint32_t>(cancelled ? 1 : 0);
        return HilStatus::done;
    }

    void SpiSlaveCommands::TransferDone()
    {
        armed = false;
        done = true;

        if (waiting)
        {
            waitTimer.Cancel();
            waiting = false;
            Report();
        }
    }

    void SpiSlaveCommands::Report()
    {
        const infra::ConstByteRange data = infra::Head(infra::MakeRange(receiveBuffer), receiveSize);
        auto line = Context().response.Ok();
        line << " done=1";

        if (output == Output::hex)
        {
            line << " rx=";
            line.Hex(data);
        }
        else
            PrintData(line, data, Output::crc);
    }

    void SpiSlaveCommands::StopWaiting()
    {
        if (!waiting)
            return;

        waitTimer.Cancel();
        waiting = false;
        Context().response.Ok() << " done=0";
    }
}
