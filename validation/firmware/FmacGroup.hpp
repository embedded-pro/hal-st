#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_FMAC)

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/FmacStm.hpp"
#include "hal_st/stm32fxxx/FmacStmDma.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousFmacStm.hpp"
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
    class FmacCommands
        : public services::TerminalCommands
    {
    public:
        enum class Variant : uint8_t
        {
            poll,
            interrupt,
            dma,
        };

        enum class Store : uint8_t
        {
            b,
            a,
            x,
            y,
        };

        FmacCommands(services::HilContext& context, hal::DmaStm& dma, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Filter
        {
            bool iir = false;
            uint32_t p = 0;
            uint32_t q = 0;
            uint32_t gain = 0;
            bool clip = false;
            uint32_t history = 0;
            uint32_t outputHistory = 0;
            Variant variant = Variant::poll;
        };

        services::HilStatus Load(const services::HilArguments& arguments);
        infra::MemoryRange<int16_t> StoreRange(Store store);
        services::HilStatus Fir(const services::HilArguments& arguments);
        services::HilStatus Iir(const services::HilArguments& arguments);
        services::HilStatus Run(const services::HilArguments& arguments, Filter filter);
        services::HilStatus ParseFilter(const services::HilArguments& arguments, Filter& filter);
        services::HilStatus Reserve();
        services::HilStatus ClaimDma();
        void Release();
        void Configure(hal::FmacBaseStm& fmac, const Filter& filter);
        void Reply(infra::MemoryRange<const int16_t> data, bool saturated, uint32_t elapsed);
        void FilterAsync(const Filter& filter, infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> output);

    private:
        services::HilContext& context;
        hal::DmaStm& dma;
        ResourceAllocation& resources;
        services::HilPendingOperation pending;
        Stopwatch stopwatch;
        Output output{ Output::hex };
        bool dmaClaimed = false;
        infra::TimePoint deadline;
        std::array<int16_t, hal::FmacBaseStm::maximumFirTaps> b{};
        std::array<int16_t, hal::FmacBaseStm::maximumIirFeedback> a{};
        std::array<int16_t, hal::FmacBaseStm::maximumFirTaps - 1> x{};
        std::array<int16_t, hal::FmacBaseStm::maximumIirFeedback> y{};
        std::array<int16_t, hal::FmacBaseStm::maximumFirTaps - 1> preload{};
        std::array<int16_t, hal::FmacBaseStm::maximumIirFeedback> outputPreload{};
        alignas(int16_t) std::array<uint8_t, 2048> inputBytes{};
        std::array<int16_t, 1024> results{};
        infra::MemoryRange<int16_t> pendingResult;
        std::optional<hal::DmaStm::TransmitStream> writeStream;
        std::optional<hal::DmaStm::ReceiveStream> readStream;
        std::variant<std::monostate, hal::SynchronousFmacStm, hal::FmacStm, hal::FmacStmDma> driver;
        std::array<Command, 3> commands;
    };
}

#endif
