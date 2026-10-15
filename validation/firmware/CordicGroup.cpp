#include "validation/firmware/CordicGroup.hpp"

#if defined(HAS_PERIPHERAL_CORDIC)

#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/firmware/Owners.hpp"
#include <chrono>

namespace validation
{
    namespace
    {
        using services::HilStatus;
        using Config = hal::CordicBaseStm::Config;

        constexpr std::chrono::milliseconds timeout{ 1000 };

        constexpr std::array<const char*, 11> keys{ { "len", "pattern", "seed", "precision", "scale", "insize", "outsize", "nargs", "nres", "variant", "out" } };

        constexpr std::array<services::HilChoice<Config::Function>, 10> functionChoices{ {
            { "cos", Config::Function::cosine },
            { "sin", Config::Function::sine },
            { "phase", Config::Function::phase },
            { "mod", Config::Function::modulus },
            { "atan", Config::Function::arctangent },
            { "cosh", Config::Function::hyperbolicCosine },
            { "sinh", Config::Function::hyperbolicSine },
            { "atanh", Config::Function::hyperbolicArctangent },
            { "ln", Config::Function::naturalLogarithm },
            { "sqrt", Config::Function::squareRoot },
        } };

        constexpr std::array<services::HilChoice<Config::Width>, 2> widthChoices{ {
            { "32", Config::Width::q31 },
            { "16", Config::Width::q15 },
        } };

        constexpr std::array<services::HilChoice<CordicCommands::Variant>, 4> variantChoices{ {
            { "poll", CordicCommands::Variant::poll },
            { "zo", CordicCommands::Variant::zeroOverhead },
            { "it", CordicCommands::Variant::interrupt },
            { "dma", CordicCommands::Variant::dma },
        } };

        constexpr Resource ResourceOf(DmaChannel channel)
        {
            return channel.dma == 1 ? Resource::dma1 : Resource::dma2;
        }

        std::size_t WordsPer(Config::Width width, uint8_t count)
        {
            return width == Config::Width::q15 ? 1 : count;
        }
    }

    CordicCommands::CordicCommands(services::HilContext& context, hal::DmaStm& dma, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , dma(dma)
        , resources(resources)
        , pending(context.response)
        , commands{ {
              services::HilBind<CordicCommands, &CordicCommands::Calculate>("cordic.calc", "<cos|sin|phase|mod|atan|cosh|sinh|atanh|ln|sqrt> <hex|-> [key=value]...", *this, context.response),
          } }
    {}

    infra::MemoryRange<const CordicCommands::Command> CordicCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus CordicCommands::Calculate(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, infra::MakeRange(keys)))
            return HilStatus::usage;

        Config config;
        auto variant = Variant::poll;
        auto status = ParseConfig(arguments, config, variant, output);
        if (status != HilStatus::done)
            return status;

        infra::ByteRange payload;
        status = ParsePayload(arguments, 1, infra::MakeRange(argumentBytes), payload);
        if (status != HilStatus::done)
            return status;

        auto argumentWords = WordsPer(config.input, config.arguments);
        if (payload.empty() || payload.size() % (sizeof(int32_t) * argumentWords) != 0)
            return HilStatus::usage;

        auto calculations = payload.size() / sizeof(int32_t) / argumentWords;
        auto resultWords = calculations * WordsPer(config.output, config.results);
        status = CheckOutput(resultWords * sizeof(int32_t), output);
        if (status != HilStatus::done)
            return status;

        status = Reserve();
        if (status != HilStatus::done)
            return status;

        auto input = infra::ReinterpretCastMemoryRange<const int32_t>(infra::Head(infra::MakeRange(argumentBytes), payload.size()));
        auto result = infra::Head(infra::MakeRange(results), resultWords);

        if (variant == Variant::interrupt || variant == Variant::dma)
        {
            if (variant == Variant::dma)
            {
                status = ClaimDma();
                if (status != HilStatus::done)
                    return status;
            }

            CalculateAsync(config, variant, input, result);
            return HilStatus::done;
        }

        auto& cordic = driver.emplace<hal::SynchronousCordicStm>(config);
        stopwatch.Start();
        auto calculated = variant == Variant::poll ? cordic.Calculate(input, result) : cordic.CalculateZeroOverhead(input, result);
        auto elapsed = stopwatch.ElapsedUs();
        Release();

        if (!calculated)
            return HilStatus::failed;

        Reply(infra::ReinterpretCastByteRange(result), elapsed);
        return HilStatus::done;
    }

    HilStatus CordicCommands::ParseConfig(const services::HilArguments& arguments, Config& config, Variant& variant, Output& output) const
    {
        auto status = HilStatus::done;
        uint32_t precision = config.precision;
        uint32_t scale = config.scale;
        uint32_t argumentCount = config.arguments;
        uint32_t resultCount = config.results;

        arguments.SelectAt(0, config.function, functionChoices, status);
        arguments.Select("insize", config.input, widthChoices, status);
        arguments.Select("outsize", config.output, widthChoices, status);
        arguments.Select("variant", variant, variantChoices, status);
        arguments.Number("precision", precision, 1, hal::CordicBaseStm::maximumPrecision, status);
        arguments.Number("scale", scale, 0, hal::CordicBaseStm::maximumScale, status);
        arguments.Number("nargs", argumentCount, 1, 2, status);
        arguments.Number("nres", resultCount, 1, 2, status);
        if (status != HilStatus::done)
            return status;

        config.precision = static_cast<uint8_t>(precision);
        config.scale = static_cast<uint8_t>(scale);
        config.arguments = static_cast<uint8_t>(argumentCount);
        config.results = static_cast<uint8_t>(resultCount);
        return ParseOutput(arguments, output);
    }

    HilStatus CordicCommands::Reserve()
    {
        if (pending.Busy())
        {
            if (infra::Now() < deadline)
                return HilStatus::busy;

            // An operation whose ERR timeout went out may never complete; the next command may start another one
            pending.Cancel();
        }

        Release();
        return HilStatus::done;
    }

    HilStatus CordicCommands::ClaimDma()
    {
        auto status = resources.Claim(ResourceOf(board::cordicDma.transmit), board::cordicDma.transmit.channel, owners::cordic);
        if (status == HilStatus::done)
            status = resources.Claim(ResourceOf(board::cordicDma.receive), board::cordicDma.receive.channel, owners::cordic);

        dmaClaimed = true;
        if (status != HilStatus::done)
            Release();

        return status;
    }

    void CordicCommands::Release()
    {
        driver.emplace<std::monostate>();
        writeStream.reset();
        readStream.reset();

        if (dmaClaimed)
        {
            resources.Release(ResourceOf(board::cordicDma.transmit), board::cordicDma.transmit.channel, owners::cordic);
            resources.Release(ResourceOf(board::cordicDma.receive), board::cordicDma.receive.channel, owners::cordic);
            dmaClaimed = false;
        }
    }

    void CordicCommands::Reply(infra::ConstByteRange data, uint32_t elapsed)
    {
        auto line = context.response.Ok();
        PrintData(line, data, output);
        line << " us=" << elapsed;
    }

    void CordicCommands::CalculateAsync(Config config, Variant variant, infra::MemoryRange<const int32_t> input, infra::MemoryRange<int32_t> result)
    {
        pendingResult = result;
        deadline = infra::Now() + timeout;
        auto onDone = [this, operation = pending.Start(timeout)]()
        {
            auto elapsed = stopwatch.ElapsedUs();

            // The driver calls this from its own handler, so it is destroyed after the handler returns
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    if (!pending.Busy())
                        Release();
                });

            if (pending.Complete(operation))
                Reply(infra::ReinterpretCastByteRange(pendingResult), elapsed);
        };

        if (variant == Variant::dma)
        {
            auto& write = writeStream.emplace(dma, hal::DmaChannelId(board::cordicDma.transmit.dma, board::cordicDma.transmit.channel, DMA_REQUEST_CORDIC_WRITE));
            auto& read = readStream.emplace(dma, hal::DmaChannelId(board::cordicDma.receive.dma, board::cordicDma.receive.channel, DMA_REQUEST_CORDIC_READ));
            auto& cordic = driver.emplace<hal::CordicStmDma>(write, read, config);
            stopwatch.Start();
            cordic.Calculate(input, result, onDone);
        }
        else
        {
            auto& cordic = driver.emplace<hal::CordicStm>(config);
            stopwatch.Start();
            cordic.Calculate(input, result, onDone);
        }
    }
}

#endif
