#include "validation/firmware/SpiFactory.hpp"
#include "BoardProfile.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <array>
#include <cstddef>
#include <limits>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr std::array<const char*, 8> openKeys{ { "clk", "mosi", "miso", "cs", "baud", "mode", "dma", "sync" } };

        constexpr std::array<uint32_t, 8> baudRatePrescalers{ {
            SPI_BAUDRATEPRESCALER_2,
            SPI_BAUDRATEPRESCALER_4,
            SPI_BAUDRATEPRESCALER_8,
            SPI_BAUDRATEPRESCALER_16,
            SPI_BAUDRATEPRESCALER_32,
            SPI_BAUDRATEPRESCALER_64,
            SPI_BAUDRATEPRESCALER_128,
            SPI_BAUDRATEPRESCALER_256,
        } };

        bool InstanceExists(uint8_t index)
        {
            return index >= 1 && index <= hal::peripheralSpi.size() && hal::peripheralSpi[index - 1] != nullptr;
        }

        std::optional<uint32_t> BaudRatePrescaler(uint32_t kernelClock, uint32_t baud)
        {
            const auto rate = static_cast<uint64_t>(baud);
            if (rate * 2 > kernelClock)
                return std::nullopt;

            for (std::size_t n = 0; n != baudRatePrescalers.size(); ++n)
                if ((rate << (n + 1)) >= kernelClock)
                    return baudRatePrescalers[n];

            return std::nullopt;
        }

        bool PinsSupportFunctions(uint8_t index, HilPinId clock, HilPinId mosi, HilPinId miso)
        {
            return SupportsFunction(clock, hal::PinConfigTypeStm::spiClock, index) && SupportsFunction(mosi, hal::PinConfigTypeStm::spiMosi, index) && SupportsFunction(miso, hal::PinConfigTypeStm::spiMiso, index);
        }

        template<class Config>
        Config MakeConfig(const auto& request)
        {
            Config config;
            config.polarityLow = (request.mode & 2) == 0;
            config.phase1st = (request.mode & 1) == 0;
            config.baudRatePrescaler = request.baudRatePrescaler;
            return config;
        }
    }

    SpiFactoryStm::SpiFactoryStm(const services::HilPinNaming& naming, hal::DmaStm& dma)
        : naming(naming)
        , dma(dma)
    {}

    uint8_t SpiFactoryStm::Instances() const
    {
        return 4;
    }

    infra::MemoryRange<const char* const> SpiFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus SpiFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    HilStatus SpiFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilSpiHandle& handle)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        ClaimedPins claimed;
        status = Claim(index, request, pins, claimed);
        if (status != HilStatus::done)
            return status;

        Construct(index, request, claimed, handle);
        return HilStatus::done;
    }

    void SpiFactoryStm::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        // The driver goes first: an end-of-transfer interrupt must not reach a destroyed chip-select wrapper
        driver.emplace<std::monostate>();
        chipSelect.reset();
        synchronousChipSelect.reset();
        receiveStream.reset();
        transmitStream.reset();
        onClosed();
    }

    HilStatus SpiFactoryStm::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (!InstanceExists(index))
            return HilStatus::range;

        const HilStatus status = Parse(arguments, request);
        if (status != HilStatus::done)
            return status;

        if (!request.clock || !request.mosi || !request.miso)
            return HilStatus::usage;

        if (request.dma && request.synchronous)
            return HilStatus::usage;

        const auto prescaler = BaudRatePrescaler(board::SpiKernelClock(index), request.baud);
        if (!prescaler)
            return HilStatus::range;

        request.baudRatePrescaler = *prescaler;

        if (!PinsSupportFunctions(index, *request.clock, *request.mosi, *request.miso))
            return HilStatus::pin;

        if (request.chipSelect && !IsBonded(*request.chipSelect))
            return HilStatus::pin;

        if (request.dma && !board::SpiDma(index))
            return HilStatus::unsupported;

        return HilStatus::done;
    }

    HilStatus SpiFactoryStm::Parse(const services::HilArguments& arguments, Request& request) const
    {
        HilStatus status = HilStatus::done;
        arguments.Pin("clk", naming, request.clock, status);
        arguments.Pin("mosi", naming, request.mosi, status);
        arguments.Pin("miso", naming, request.miso, status);
        arguments.Pin("cs", naming, request.chipSelect, status);
        arguments.Number("baud", request.baud, 1, std::numeric_limits<uint32_t>::max(), status);
        arguments.Number("mode", request.mode, 0, 3, status);
        arguments.Flag("dma", request.dma, status);
        arguments.Flag("sync", request.synchronous, status);
        return status;
    }

    HilStatus SpiFactoryStm::Claim(uint8_t index, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const
    {
        HilStatus status = pins.ClaimFunction(request.clock, Function(hal::PinConfigTypeStm::spiClock), index, claimed.clock);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.mosi, Function(hal::PinConfigTypeStm::spiMosi), index, claimed.mosi);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.miso, Function(hal::PinConfigTypeStm::spiMiso), index, claimed.miso);
        if (status == HilStatus::done && request.chipSelect)
            status = pins.Claim(*request.chipSelect, services::HilPinPool::Use::exclusive, claimed.chipSelect);

        return status;
    }

    void SpiFactoryStm::Construct(uint8_t index, const Request& request, const ClaimedPins& claimed, services::HilSpiHandle& handle)
    {
        if (request.synchronous)
        {
            auto& spi = driver.emplace<hal::SynchronousSpiMasterStm>(index, PinOrDummy(claimed.clock), PinOrDummy(claimed.miso), PinOrDummy(claimed.mosi), MakeConfig<hal::SynchronousSpiMasterStm::Config>(request));

            if (claimed.chipSelect != nullptr)
                handle.synchronous = &synchronousChipSelect.emplace(spi, *claimed.chipSelect);
            else
                handle.synchronous = &spi;

            return;
        }

        auto& spi = ConstructAsynchronous(index, request, claimed);

        if (claimed.chipSelect != nullptr)
            handle.spi = &chipSelect.emplace(spi, *claimed.chipSelect);
        else
            handle.spi = &spi;
    }

    hal::SpiMaster& SpiFactoryStm::ConstructAsynchronous(uint8_t index, const Request& request, const ClaimedPins& claimed)
    {
        if (!request.dma)
            return driver.emplace<hal::SpiMasterStm>(index, PinOrDummy(claimed.clock), PinOrDummy(claimed.miso), PinOrDummy(claimed.mosi), MakeConfig<hal::SpiMasterStm::Config>(request));

        const DmaRequests requests = *board::SpiDma(index);
        transmitStream.emplace(dma, hal::DmaChannelId(1, board::spiDmaChannel, requests.transmit));
        receiveStream.emplace(dma, hal::DmaChannelId(1, static_cast<uint8_t>(board::spiDmaChannel + 1), requests.receive));

        return driver.emplace<hal::SpiMasterStmDma>(*transmitStream, *receiveStream, index, PinOrDummy(claimed.clock), PinOrDummy(claimed.miso), PinOrDummy(claimed.mosi), MakeConfig<hal::SpiMasterStmDma::Config>(request));
    }
}
