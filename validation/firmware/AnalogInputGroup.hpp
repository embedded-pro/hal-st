#pragma once

#include "hal_st/stm32fxxx/AdcDmaStm.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/util/Terminal.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class AnalogInputCommands
        : public services::TerminalCommands
    {
    public:
        AnalogInputCommands(services::HilContext& context, const services::HilPinNaming& naming, hal::DmaStm& dma, TimerAllocation& timers, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        static constexpr std::size_t bufferSize = 256;

        enum class Output : uint8_t
        {
            list,
            stats,
        };

        struct BurstRequest
        {
            HilPinId pin;
            uint32_t samples = 0;
            uint32_t rate = 0;
            uint32_t repeat = 1;
            Output output = Output::list;
        };

        services::HilStatus Read(const services::HilArguments& arguments);
        services::HilStatus Burst(const services::HilArguments& arguments);
        services::HilStatus ParseAnalogPin(infra::BoundedConstString text, HilPinId& pin) const;
        services::HilStatus Claim(const std::optional<HilPinId>& pin, bool burst);
        void Release();
        void FinishRead();
        void StartRun();
        void RunDone(infra::MemoryRange<uint16_t> samples);
        void FinishBurst();
        void PrintRun(services::HilResponse::Line& line, infra::MemoryRange<const uint16_t> samples, uint32_t microseconds) const;

    private:
        services::HilContext& context;
        const services::HilPinNaming& naming;
        hal::DmaStm& dma;
        TimerAllocation& timers;
        ResourceAllocation& resources;
        services::HilPinOwner pins;
        services::HilPendingOperation pending;
        hal::GpioPin* claimedPin = nullptr;
        std::optional<hal::AdcStm> adc;
        std::variant<std::monostate, hal::AnalogToDigitalPinImplStm, hal::AnalogToDigitalInternalTemperatureStm> reader;
        std::optional<hal::DmaStm::ReceiveStream> stream;
        std::optional<hal::AdcTriggeredByTimerWithDma> burst;
        std::array<uint16_t, bufferSize> buffer{};
        BurstRequest request;
        uint32_t operation = 0;
        uint16_t code = 0;
        bool temperature = false;
        uint32_t completedRuns = 0;
        infra::MemoryRange<const uint16_t> lastSamples;
        uint32_t lastMicroseconds = 0;
        Stopwatch stopwatch;
        std::array<Command, 2> commands;
    };
}
