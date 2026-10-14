#include "validation/firmware/FmacGroup.hpp"

#if defined(HAS_PERIPHERAL_FMAC)

#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/firmware/Owners.hpp"
#include <algorithm>
#include <chrono>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr std::chrono::milliseconds timeout{ 1000 };
        constexpr std::size_t loadBytes = 2 * hal::FmacBaseStm::maximumFirTaps;

        constexpr std::array<const char*, 9> firKeys{ { "taps", "gain", "clip", "hist", "variant", "out", "len", "pattern", "seed" } };
        constexpr std::array<const char*, 11> iirKeys{ { "p", "q", "gain", "clip", "hist", "yhist", "variant", "out", "len", "pattern", "seed" } };

        constexpr std::array<services::HilChoice<FmacCommands::Store>, 4> storeChoices{ {
            { "b", FmacCommands::Store::b },
            { "a", FmacCommands::Store::a },
            { "x", FmacCommands::Store::x },
            { "y", FmacCommands::Store::y },
        } };

        constexpr std::array<services::HilChoice<FmacCommands::Variant>, 3> variantChoices{ {
            { "poll", FmacCommands::Variant::poll },
            { "it", FmacCommands::Variant::interrupt },
            { "dma", FmacCommands::Variant::dma },
        } };

        constexpr Resource ResourceOf(DmaChannel channel)
        {
            return channel.dma == 1 ? Resource::dma1 : Resource::dma2;
        }

        void FillHistory(infra::MemoryRange<int16_t> target, infra::MemoryRange<const int16_t> stored)
        {
            auto kept = std::min(stored.size(), target.size());
            std::fill(target.begin(), target.end(), 0);
            std::copy(stored.end() - kept, stored.end(), target.end() - kept);
        }
    }

    FmacCommands::FmacCommands(services::HilContext& context, hal::DmaStm& dma, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , dma(dma)
        , resources(resources)
        , pending(context.response)
        , commands{ {
              services::HilBind<FmacCommands, &FmacCommands::Load>("fmac.load", "<b|a|x|y> <offset> <hex> [clear=]", *this, context.response),
              services::HilBind<FmacCommands, &FmacCommands::Fir>("fmac.fir", "<hex|-> taps=<2-127> [key=value]...", *this, context.response),
              services::HilBind<FmacCommands, &FmacCommands::Iir>("fmac.iir", "<hex|-> p=<2-64> q=<1-63> [key=value]...", *this, context.response),
          } }
    {}

    infra::MemoryRange<const FmacCommands::Command> FmacCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus FmacCommands::Load(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(3, 3, { "clear" }))
            return HilStatus::usage;

        auto status = HilStatus::done;
        auto store = Store::b;
        uint32_t offset = 0;
        bool clear = false;
        arguments.SelectAt(0, store, storeChoices, status);
        arguments.NumberAt(1, offset, 0, hal::FmacBaseStm::maximumFirTaps - 1, status);
        arguments.Flag("clear", clear, status);
        if (status != HilStatus::done)
            return status;

        std::array<uint8_t, loadBytes> bytes{};
        std::size_t size = 0;
        status = services::HilArguments::ParseHex(arguments.Positional(2), infra::MakeRange(bytes), size);
        if (status != HilStatus::done)
            return status;

        if (size == 0 || size % sizeof(int16_t) != 0)
            return HilStatus::usage;

        auto target = StoreRange(store);
        auto count = size / sizeof(int16_t);
        if (offset + count > target.size())
            return HilStatus::range;

        if (clear)
            std::fill(target.begin(), target.end(), 0);

        for (std::size_t i = 0; i != count; ++i)
            target[offset + i] = static_cast<int16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));

        context.response.Ok() << " size=" << static_cast<uint32_t>(offset + count);
        return HilStatus::done;
    }

    infra::MemoryRange<int16_t> FmacCommands::StoreRange(Store store)
    {
        switch (store)
        {
            case Store::b:
                return infra::MakeRange(b);
            case Store::a:
                return infra::MakeRange(a);
            case Store::x:
                return infra::MakeRange(x);
            default:
                return infra::MakeRange(y);
        }
    }

    HilStatus FmacCommands::Fir(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(firKeys)) || !arguments.Has("taps"))
            return HilStatus::usage;

        Filter filter;
        auto status = HilStatus::done;
        arguments.Number("taps", filter.p, 2, hal::FmacBaseStm::maximumFirTaps, status);
        if (status != HilStatus::done)
            return status;

        return Run(arguments, filter);
    }

    HilStatus FmacCommands::Iir(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(iirKeys)) || !arguments.Has("p") || !arguments.Has("q"))
            return HilStatus::usage;

        Filter filter;
        filter.iir = true;
        auto status = HilStatus::done;
        arguments.Number("p", filter.p, 2, hal::FmacBaseStm::maximumIirFeedForward, status);
        arguments.Number("q", filter.q, 1, hal::FmacBaseStm::maximumIirFeedback, status);
        arguments.Number("yhist", filter.outputHistory, 0, y.size(), status);
        if (status != HilStatus::done)
            return status;

        return Run(arguments, filter);
    }

    HilStatus FmacCommands::Run(const services::HilArguments& arguments, Filter filter)
    {
        auto status = ParseFilter(arguments, filter);
        if (status != HilStatus::done)
            return status;

        infra::ByteRange payload;
        status = ParsePayload(arguments, 0, infra::MakeRange(inputBytes), payload);
        if (status != HilStatus::done)
            return status;

        if (payload.empty() || payload.size() % sizeof(int16_t) != 0)
            return HilStatus::usage;

        status = CheckOutput(payload.size(), output);
        if (status != HilStatus::done)
            return status;

        status = Reserve();
        if (status != HilStatus::done)
            return status;

        auto input = infra::ReinterpretCastMemoryRange<const int16_t>(infra::Head(infra::MakeRange(inputBytes), payload.size()));
        auto result = infra::Head(infra::MakeRange(results), input.size());

        if (filter.variant != Variant::poll)
        {
            if (filter.variant == Variant::dma)
            {
                status = ClaimDma();
                if (status != HilStatus::done)
                    return status;
            }

            FilterAsync(filter, input, result);
            return HilStatus::done;
        }

        hal::FmacBaseStm::Config config;
        config.clip = filter.clip;
        auto& fmac = driver.emplace<hal::SynchronousFmacStm>(config);
        Configure(fmac, filter);
        stopwatch.Start();
        auto saturated = fmac.Filter(input, result);
        auto elapsed = stopwatch.ElapsedUs();
        Release();

        if (!saturated)
            return HilStatus::failed;

        Reply(result, *saturated, elapsed);
        return HilStatus::done;
    }

    HilStatus FmacCommands::ParseFilter(const services::HilArguments& arguments, Filter& filter)
    {
        auto status = HilStatus::done;
        arguments.Number("gain", filter.gain, 0, hal::FmacBaseStm::maximumGain, status);
        arguments.Flag("clip", filter.clip, status);
        arguments.Number("hist", filter.history, 0, x.size(), status);
        arguments.Select("variant", filter.variant, variantChoices, status);
        if (status != HilStatus::done)
            return status;

        return ParseOutput(arguments, output);
    }

    HilStatus FmacCommands::Reserve()
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

    HilStatus FmacCommands::ClaimDma()
    {
        auto status = resources.Claim(ResourceOf(board::fmacDma.transmit), board::fmacDma.transmit.channel, owners::fmac);
        if (status == HilStatus::done)
            status = resources.Claim(ResourceOf(board::fmacDma.receive), board::fmacDma.receive.channel, owners::fmac);

        dmaClaimed = true;
        if (status != HilStatus::done)
            Release();

        return status;
    }

    void FmacCommands::Release()
    {
        driver.emplace<std::monostate>();
        writeStream.reset();
        readStream.reset();

        if (dmaClaimed)
        {
            resources.Release(ResourceOf(board::fmacDma.transmit), board::fmacDma.transmit.channel, owners::fmac);
            resources.Release(ResourceOf(board::fmacDma.receive), board::fmacDma.receive.channel, owners::fmac);
            dmaClaimed = false;
        }
    }

    void FmacCommands::Configure(hal::FmacBaseStm& fmac, const Filter& filter)
    {
        auto history = infra::Head(infra::MakeRange(preload), filter.p - 1);
        FillHistory(history, infra::Head(infra::MakeRange(x), filter.history));

        if (!filter.iir)
        {
            fmac.ConfigureFir(infra::Head(infra::MakeRange(b), filter.p), static_cast<uint8_t>(filter.gain), history);
            return;
        }

        auto outputHistory = infra::Head(infra::MakeRange(outputPreload), filter.q);
        FillHistory(outputHistory, infra::Head(infra::MakeRange(y), filter.outputHistory));
        fmac.ConfigureIir(infra::Head(infra::MakeRange(b), filter.p), infra::Head(infra::MakeRange(a), filter.q), static_cast<uint8_t>(filter.gain), history, outputHistory);
    }

    void FmacCommands::Reply(infra::MemoryRange<const int16_t> data, bool saturated, uint32_t elapsed)
    {
        auto line = context.response.Ok();
        PrintData(line, infra::ReinterpretCastByteRange(data), output);
        line << " sat=" << (saturated ? 1u : 0u) << " us=" << elapsed;
    }

    void FmacCommands::FilterAsync(const Filter& filter, infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> result)
    {
        pendingResult = result;
        deadline = infra::Now() + timeout;
        auto onDone = [this, operation = pending.Start(timeout)](bool saturated)
        {
            auto elapsed = stopwatch.ElapsedUs();

            // The driver calls this from its own handler, so it is destroyed after the handler returns
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    if (!pending.Busy())
                        Release();
                });

            if (pending.Complete(operation))
                Reply(pendingResult, saturated, elapsed);
        };

        hal::FmacBaseStm::Config config;
        config.clip = filter.clip;

        if (filter.variant == Variant::dma)
        {
            auto& write = writeStream.emplace(dma, hal::DmaChannelId(board::fmacDma.transmit.dma, board::fmacDma.transmit.channel, DMA_REQUEST_FMAC_WRITE));
            auto& read = readStream.emplace(dma, hal::DmaChannelId(board::fmacDma.receive.dma, board::fmacDma.receive.channel, DMA_REQUEST_FMAC_READ));
            auto& fmac = driver.emplace<hal::FmacStmDma>(write, read, config);
            Configure(fmac, filter);
            stopwatch.Start();
            fmac.Filter(input, result, onDone);
        }
        else
        {
            auto& fmac = driver.emplace<hal::FmacStm>(config);
            Configure(fmac, filter);
            stopwatch.Start();
            fmac.Filter(input, result, onDone);
        }
    }
}

#endif
