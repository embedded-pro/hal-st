#include "validation/firmware/RngGroup.hpp"
#include "infra/util/Endian.hpp"
#include "validation/firmware/Payload.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <limits>
#include DEVICE_HEADER
#if defined(STM32WBA)
#include "stm32wbaxx_ll_rng.h"
#elif defined(STM32WB)
#include "hal_st/synchronous_stm32fxxx/SynchronousSynchronizedRandomDataGeneratorStm.hpp"
#include "stm32wbxx_ll_rng.h"
#include "validation/firmware/HsemMaster.hpp"
#endif

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;
        using Variant = RngCommands::Variant;

        constexpr std::array<const char*, 2> readKeys{ { "variant", "lock5" } };
        constexpr std::array<const char*, 1> statsKeys{ { "variant" } };

        constexpr std::array<HilChoice<Variant>, 3> variants{ {
            { "sync", Variant::sync },
            { "async", Variant::async },
            { "hsem", Variant::hsem },
        } };

        constexpr uint32_t minimumStats = 16;
        constexpr uint32_t maximumStats = 65536;
        constexpr std::chrono::milliseconds readTimeout{ 1000 };
        constexpr std::chrono::milliseconds statsTimeout{ 5000 };
    }

    RngCommands::RngCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , pending(context.response)
        , commands{ {
              services::HilBind<RngCommands, &RngCommands::Read>("rng.read", "<len> [variant=sync|async|hsem] [lock5=0|1]", *this, context.response),
              services::HilBind<RngCommands, &RngCommands::Stats>("rng.stats", "<len> [variant=sync|async|hsem]", *this, context.response),
          } }
    {}

    infra::MemoryRange<const RngCommands::Command> RngCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus RngCommands::Read(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(readKeys)))
            return HilStatus::usage;

        Request newRequest;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, newRequest.length, 1, maximumHexOutput, status);
        arguments.Select("variant", newRequest.variant, variants, status);
        arguments.Flag("lock5", newRequest.lockClock, status);
        if (status != HilStatus::done)
            return status;

        return Run(newRequest);
    }

    HilStatus RngCommands::Stats(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(statsKeys)))
            return HilStatus::usage;

        Request newRequest;
        newRequest.job = Job::stats;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, newRequest.length, minimumStats, maximumStats, status);
        arguments.Select("variant", newRequest.variant, variants, status);
        if (status != HilStatus::done)
            return status;

        return Run(newRequest);
    }

    HilStatus RngCommands::Check(const Request& candidate) const
    {
#if !defined(STM32WB)
        if (candidate.variant == Variant::hsem || candidate.lockClock)
            return HilStatus::unsupported;
#endif

        if (candidate.lockClock && candidate.variant != Variant::hsem)
            return HilStatus::usage;

        return HilStatus::done;
    }

    HilStatus RngCommands::Reserve()
    {
        if (!pending.Busy())
            return HilStatus::done;

        if (infra::Now() < deadline)
            return HilStatus::busy;

        // A run whose ERR timeout went out may never complete; drop it so the RNG serves the next command
        pending.Cancel();
        operation = 0;
        LL_RNG_DisableIT(RNG);
        asyncGenerator.reset();
        return HilStatus::done;
    }

    HilStatus RngCommands::Run(const Request& newRequest)
    {
        auto status = Check(newRequest);
        if (status == HilStatus::done)
            status = Reserve();
        if (status != HilStatus::done)
            return status;

        request = newRequest;
        remaining = request.length;
        statistics.Reset();

        if (request.variant != Variant::async)
            return RunSynchronous();

        StartAsynchronous();
        return HilStatus::done;
    }

    HilStatus RngCommands::RunSynchronous()
    {
#if defined(STM32WB)
        if (request.variant == Variant::hsem)
        {
            constexpr auto clockSemaphore = static_cast<uint32_t>(hal::Semaphore::recoveryAndIndependentClockConfiguration);
            auto& master = HsemMaster();

            // With semaphore 5 locked, Hsi48Enabler asserts that HSI48 runs instead of starting it
            if (request.lockClock && LL_RCC_HSI48_IsReady() == 0)
                return HilStatus::failed;

            if (request.lockClock && HAL_HSEM_FastTake(clockSemaphore) != HAL_OK)
                return HilStatus::busy;

            hal::SynchronousSynchronizedRandomDataGeneratorStm generator{ creator, master };
            Generate(generator);

            if (request.lockClock)
                HAL_HSEM_Release(clockSemaphore, 0);

            Finish();
            return HilStatus::done;
        }
#endif

        hal::SynchronousRandomDataGeneratorStm generator;
        Generate(generator);
        Finish();
        return HilStatus::done;
    }

    void RngCommands::Generate(hal::SynchronousRandomDataGenerator& generator)
    {
        stopwatch.Start();

        while (remaining != 0)
        {
            auto chunk = NextChunk();
            generator.GenerateRandomData(chunk);
            Consume(chunk);
        }
    }

    void RngCommands::StartAsynchronous()
    {
        auto timeout = request.job == Job::read ? readTimeout : statsTimeout;
        asyncGenerator.emplace();
        operation = pending.Start(timeout);
        deadline = infra::Now() + timeout;
        stopwatch.Start();
        RequestChunk();
    }

    void RngCommands::RequestChunk()
    {
        asyncGenerator->GenerateRandomData(NextChunk(), [this, chunkOperation = operation]()
            {
                ChunkDone(chunkOperation);
            });
    }

    void RngCommands::ChunkDone(uint32_t chunkOperation)
    {
        if (chunkOperation != operation)
            return;

        Consume(infra::Head(infra::MakeRange(buffer), chunkSize));

        if (remaining != 0)
        {
            RequestChunk();
            return;
        }

        operation = 0;
        asyncGenerator.reset();

        if (pending.Complete(chunkOperation))
            Finish();
    }

    infra::ByteRange RngCommands::NextChunk()
    {
        chunkSize = std::min<uint32_t>(remaining, buffer.size());
        return infra::Head(infra::MakeRange(buffer), chunkSize);
    }

    void RngCommands::Consume(infra::ConstByteRange chunk)
    {
        if (request.job == Job::stats)
            statistics.Add(chunk);

        remaining -= chunk.size();
    }

    void RngCommands::Finish()
    {
        auto elapsed = stopwatch.ElapsedUs();
        auto line = context.response.Ok();

        if (request.job == Job::read)
        {
            PrintData(line, infra::Head(infra::MakeRange(buffer), request.length), Output::hex);
#if defined(STM32WB)
            if (request.variant == Variant::hsem)
                line << " hsi48=" << static_cast<uint32_t>(LL_RCC_HSI48_IsReady() != 0 ? 1 : 0);
#endif
            return;
        }

        infra::BigEndian<uint32_t> crc{ statistics.crc.Result() };
        line << " n=" << statistics.bytes << " ones=" << statistics.ones << " runs=" << statistics.transitions + 1 << " chisq=" << statistics.ChiSquareTimes1000() << " crc=";
        line.Hex(infra::MakeByteRange(crc));
        line << " us=" << elapsed;
    }

    void RngCommands::Statistics::Reset()
    {
        bytes = 0;
        ones = 0;
        transitions = 0;
        last = 0;
        histogram.fill(0);
        crc.Reset();
    }

    void RngCommands::Statistics::Add(infra::ConstByteRange data)
    {
        for (auto byte : data)
        {
            if (bytes != 0)
                transitions += (last ^ (byte >> 7)) & 1u;

            transitions += static_cast<uint32_t>(std::popcount(static_cast<uint8_t>((byte ^ (byte >> 1)) & 0x7f)));
            ones += static_cast<uint32_t>(std::popcount(byte));
            ++histogram[byte];
            last = byte;
            ++bytes;
        }

        crc.Update(data);
    }

    uint32_t RngCommands::Statistics::ChiSquareTimes1000() const
    {
        uint64_t sum = 0;

        for (auto count : histogram)
        {
            auto difference = static_cast<int64_t>(count) * static_cast<int64_t>(histogram.size()) - static_cast<int64_t>(bytes);
            sum += static_cast<uint64_t>(difference * difference);
        }

        auto value = sum * 1000 / (static_cast<uint64_t>(histogram.size()) * bytes);
        return static_cast<uint32_t>(std::min<uint64_t>(value, std::numeric_limits<uint32_t>::max()));
    }
}
