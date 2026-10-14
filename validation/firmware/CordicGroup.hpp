#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_CORDIC)

#include "hal_st/stm32fxxx/CordicStm.hpp"
#include "hal_st/stm32fxxx/CordicStmDma.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousCordicStm.hpp"
#include "infra/timer/Timer.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "validation/firmware/Payload.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class CordicCommands
        : public services::TerminalCommands
    {
    public:
        enum class Variant : uint8_t
        {
            poll,
            zeroOverhead,
            interrupt,
            dma,
        };

        CordicCommands(services::HilContext& context, hal::DmaStm& dma, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        services::HilStatus Calculate(const services::HilArguments& arguments);
        services::HilStatus ParseConfig(const services::HilArguments& arguments, hal::CordicBaseStm::Config& config, Variant& variant, Output& output) const;
        services::HilStatus Reserve();
        services::HilStatus ClaimDma();
        void Release();
        void Reply(infra::ConstByteRange data, uint32_t elapsed);
        void CalculateAsync(hal::CordicBaseStm::Config config, Variant variant, infra::MemoryRange<const int32_t> input, infra::MemoryRange<int32_t> output);

    private:
        services::HilContext& context;
        hal::DmaStm& dma;
        ResourceAllocation& resources;
        services::HilPendingOperation pending;
        Stopwatch stopwatch;
        Output output{ Output::hex };
        bool dmaClaimed = false;
        infra::TimePoint deadline;
        alignas(uint32_t) std::array<uint8_t, 2048> argumentBytes{};
        std::array<int32_t, 1024> results{};
        infra::MemoryRange<int32_t> pendingResult;
        std::optional<hal::DmaStm::TransmitStream> writeStream;
        std::optional<hal::DmaStm::ReceiveStream> readStream;
        std::variant<std::monostate, hal::SynchronousCordicStm, hal::CordicStm, hal::CordicStmDma> driver;
        std::array<Command, 1> commands;
    };
}

#endif
