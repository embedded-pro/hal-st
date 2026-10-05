#pragma once

#include "hal_st/stm32fxxx/RandomDataGeneratorStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousRandomDataGeneratorStm.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/Crc.hpp"
#include "infra/util/ProxyCreator.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace validation
{
    class RngCommands
        : public services::TerminalCommands
    {
    public:
        enum class Variant : uint8_t
        {
            sync,
            async,
            hsem,
        };

        explicit RngCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        enum class Job : uint8_t
        {
            read,
            stats,
        };

        struct Request
        {
            Job job = Job::read;
            uint32_t length = 0;
            Variant variant = Variant::sync;
            bool lockClock = false;
        };

        struct Statistics
        {
            void Reset();
            void Add(infra::ConstByteRange data);
            uint32_t ChiSquareTimes1000() const;

            uint32_t bytes = 0;
            uint32_t ones = 0;
            uint32_t transitions = 0;
            uint8_t last = 0;
            std::array<uint32_t, 256> histogram{};
            infra::Crc32 crc;
        };

        services::HilStatus Read(const services::HilArguments& arguments);
        services::HilStatus Stats(const services::HilArguments& arguments);
        services::HilStatus Check(const Request& request) const;
        services::HilStatus Reserve();
        services::HilStatus Run(const Request& request);
        services::HilStatus RunSynchronous();
        void Generate(hal::SynchronousRandomDataGenerator& generator);
        void StartAsynchronous();
        void RequestChunk();
        void ChunkDone(uint32_t chunkOperation);
        infra::ByteRange NextChunk();
        void Consume(infra::ConstByteRange chunk);
        void Finish();

    private:
        services::HilContext& context;
        services::HilPendingOperation pending;
        std::optional<hal::RandomDataGeneratorStm> asyncGenerator;
#if defined(STM32WB)
        infra::Creator<hal::SynchronousRandomDataGenerator, hal::SynchronousRandomDataGeneratorStm, void()> creator;
#endif
        Stopwatch stopwatch;
        Request request;
        Statistics statistics;
        std::array<uint8_t, 256> buffer{};
        uint32_t remaining = 0;
        uint32_t chunkSize = 0;
        uint32_t operation = 0;
        infra::TimePoint deadline;
        std::array<Command, 2> commands;
    };
}
