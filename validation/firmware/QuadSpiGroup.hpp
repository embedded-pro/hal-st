#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_QUADSPI)

#include "hal/interfaces/QuadSpi.hpp"
#include "hal/interfaces/Spi.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/QuadSpiStm.hpp"
#include "hal_st/stm32fxxx/QuadSpiStmDma.hpp"
#include "hal_st/stm32fxxx/SingleSpeedQuadSpiStmDma.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/hil/commands/HilSingleInstanceGroup.hpp"
#include "validation/firmware/Payload.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    enum class QuadSpiVariant : uint8_t
    {
        poll,
        dma,
        spi,
    };

    class QuadSpiFactoryStm
        : public services::HilInstanceFactory
    {
    public:
        QuadSpiFactoryStm(hal::DmaStm& dma, ResourceAllocation& resources);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins);
        uint32_t Clock() const;
        hal::QuadSpi& QuadSpi();
        hal::SpiMaster* Spi();

    private:
        struct Request
        {
            QuadSpiVariant variant = QuadSpiVariant::poll;
            uint32_t prescaler = 2;
            uint32_t flashSizeLog2 = 24;
        };

        using ClaimedPins = std::array<hal::GpioPin*, 6>;

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed);
        void Construct(const Request& request, const ClaimedPins& claimed);
        void Destroy();

    private:
        hal::DmaStm& dma;
        ResourceAllocation& resources;
        std::optional<hal::DmaStm::TransceiveStream> stream;
        std::variant<std::monostate, hal::QuadSpiStm, hal::QuadSpiStmDma> driver;
        std::optional<hal::SingleSpeedQuadSpiStmDma> spi;
        uint32_t clock = 0;
        infra::AutoResetFunction<void()> onClosed;
    };

    class QuadSpiGroup
        : public services::HilSingleInstanceGroup
    {
    public:
        QuadSpiGroup(services::HilContext& context, QuadSpiFactoryStm& factory);

        infra::MemoryRange<const Command> Commands() override;

    protected:
        services::HilStatus OpenInstance(uint8_t index, const services::HilArguments& arguments) override;
        void Opened(services::HilResponse::Line& line) const override;
        void CloseInstance() override;

    private:
        struct Phases
        {
            hal::QuadSpi::Header header{ std::nullopt, {}, {}, 0 };
            hal::QuadSpi::Lines lines = hal::QuadSpi::Lines::SingleSpeed();
        };

        struct Request
        {
            Phases phases;
            infra::ByteRange data;
            bool read = false;
            bool spi = false;
            Output output = Output::hex;
            uint32_t repeat = 1;
        };

        services::HilStatus Execute(const services::HilArguments& arguments);
        services::HilStatus Poll(const services::HilArguments& arguments);
        services::HilStatus Transfer(const services::HilArguments& arguments);

        services::HilStatus ParseCommandData(const services::HilArguments& arguments, Request& next);
        services::HilStatus ParseTransferData(const services::HilArguments& arguments, Request& next);

        void Start(const Request& next);
        void Issue();
        void Written(uint32_t operation);
        void Received(uint32_t operation);
        void Polled(uint32_t operation);

    private:
        QuadSpiFactoryStm& factory;
        services::HilPendingOperation pending;
        uint32_t current = 0;
        uint32_t remaining = 0;
        Request request;
        std::array<uint8_t, 256> buffer{};
        std::array<Command, 5> commands;
    };
}

#endif
