#pragma once

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/TimerStm.hpp"
#include "infra/timer/Timer.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    class DmaCommands
        : public services::TerminalCommands
    {
    public:
        DmaCommands(services::HilContext& context, const services::HilPinNaming& naming, hal::DmaStm& dma, TimerAllocation& timers, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        static constexpr std::size_t maximumPatternBytes = 32;
        static constexpr std::size_t maximumWords = maximumPatternBytes * 8;

        services::HilStatus Wave(const services::HilArguments& arguments);
        services::HilStatus Claim(HilPinId pin);
        void Release();
        std::size_t FillWords(HilPinId pin, infra::ConstByteRange pattern);
        void Finish();

    private:
        services::HilContext& context;
        const services::HilPinNaming& naming;
        hal::DmaStm& dma;
        TimerAllocation& timers;
        ResourceAllocation& resources;
        services::HilPinOwner pins;
        hal::GpioPin* claimedPin = nullptr;
        std::optional<hal::FreeRunningTimerStm> timer;
        std::optional<hal::DmaStm::TransmitStream> stream;
        std::optional<hal::CircularTransmitDmaChannel> channel;
        std::array<uint32_t, maximumWords> words{};
        infra::TimerSingleShot duration;
        std::array<Command, 1> commands;
    };
}
