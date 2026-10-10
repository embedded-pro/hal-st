#include "validation/firmware/FlashGroup.hpp"
#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/Endian.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include DEVICE_HEADER
#if defined(STM32WB)
#include "validation/firmware/HsemMaster.hpp"
#endif

extern "C"
{
    extern const uint8_t _sidata[];
    extern const uint8_t _sdata[];
    extern const uint8_t _edata[];
}

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;
        using Variant = FlashCommands::Variant;
        using Layout = FlashCommands::Layout;

        constexpr std::array<HilChoice<Variant>, 3> variants{ {
            { "sync", Variant::sync },
            { "async", Variant::async },
            { "coord", Variant::coord },
        } };

        constexpr std::array<HilChoice<Layout>, 2> layouts{ {
            { "homogeneous", Layout::homogeneous },
            { "table", Layout::table },
        } };

        constexpr std::array<const char*, 2> driverKeys{ { "variant", "layout" } };
        constexpr std::array<const char*, 5> writeKeys{ { "len", "pattern", "seed", "variant", "layout" } };
        constexpr std::array<const char*, 3> readKeys{ { "out", "variant", "layout" } };

#if defined(STM32WBA)
        constexpr uint32_t flashWord = 16;
#else
        constexpr uint32_t flashWord = 8;
#endif
        constexpr std::array<uint32_t, 4> tablePattern{ { 1, 1, 2, 4 } };
        constexpr uint32_t tablePatternPages = 8;
        constexpr uint32_t tablePatternRepeats = 2;
        constexpr std::size_t readChunk = 64;
        constexpr std::chrono::seconds eraseTimeout{ 10 };
        constexpr std::chrono::seconds transferTimeout{ 2 };

#if defined(STM32WB)
        enum class StackState : uint8_t
        {
            stopped,
            starting,
            fus,
        };

        constexpr std::array<HilChoice<StackState>, 3> stackStates{ {
            { "stopped", StackState::stopped },
            { "starting", StackState::starting },
            { "fus", StackState::fus },
        } };

        constexpr std::array<const char*, 1> stackKeys{ { "layout" } };

        // hsem.lock holds HSEM 0 as owners::scaffold; the coordinated flash needs an owner of its own to exclude it
        constexpr services::HilOwner coordinatedOwner = services::HilOwners::last;
#endif

        uint32_t Address(const uint8_t* pointer)
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
        }

        void PrintCrc(services::HilResponse::Line& line, uint32_t length, uint32_t value)
        {
            infra::BigEndian<uint32_t> crc{ value };
            line << " len=" << length << " crc=";
            line.Hex(infra::MakeByteRange(crc));
        }
    }

    FlashCommands::FlashCommands(services::HilContext& context, WatchDogFactoryStm& watchDogFactory, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , watchDogFactory(watchDogFactory)
        , resources(resources)
        , pending(context.response)
        , commands{ {
              services::HilBind<FlashCommands, &FlashCommands::Info>("flash.info", "[variant=sync|async|coord] [layout=homogeneous|table]", *this, context.response),
              services::HilBind<FlashCommands, &FlashCommands::Erase>("flash.erase", "<first> <end> [variant=] [layout=]", *this, context.response),
              services::HilBind<FlashCommands, &FlashCommands::Write>("flash.write", "<address> <hex|-> [len=] [pattern=] [seed=] [variant=] [layout=]", *this, context.response),
              services::HilBind<FlashCommands, &FlashCommands::Read>("flash.read", "<address> <len> [out=hex|crc] [variant=] [layout=]", *this, context.response),
#if defined(STM32WB)
              services::HilBind<FlashCommands, &FlashCommands::Stack>("flash.stack", "<stopped|starting|fus> [layout=]", *this, context.response),
#endif
          } }
    {
        static_assert(board::flashScratchEndPage - board::flashScratchFirstPage <= std::tuple_size_v<decltype(sectorSizes)>);

        uint32_t endPage = board::flashScratchEndPage;
#if defined(STM32WB)
        FLASH_OBProgramInitTypeDef options{};
        HAL_FLASHEx_OBGetConfig(&options);
        if (options.SecureMode == SYSTEM_IN_SECURE_MODE)
            endPage = std::min(endPage, (options.SecureFlashStartAddr - FLASH_BASE) / FLASH_PAGE_SIZE);
#endif

        const auto imageEnd = Address(_sidata) + (Address(_edata) - Address(_sdata));
        imageEndPage = (imageEnd - FLASH_BASE + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
        usable = imageEndPage <= board::flashScratchFirstPage && endPage > board::flashScratchFirstPage;

        const auto pages = usable ? endPage - board::flashScratchFirstPage : 0;
        const auto begin = reinterpret_cast<const uint8_t*>(FLASH_BASE + board::flashScratchFirstPage * FLASH_PAGE_SIZE);
        region = infra::ConstByteRange(begin, begin + pages * FLASH_PAGE_SIZE);

        // Single pages first: an unfixed erase (B.5) takes the sector index for an absolute page, so the multi-page
        // sectors need indices past the image end page to be accepted (see LayoutGeometry)
        const auto repeats = std::min(tablePatternRepeats, pages / tablePatternPages);
        for (uint32_t page = 0; page != pages - repeats * tablePatternPages; ++page)
            sectorSizes[tableSectors++] = FLASH_PAGE_SIZE;
        for (uint32_t repeat = 0; repeat != repeats; ++repeat)
            for (auto size : tablePattern)
                sectorSizes[tableSectors++] = size * FLASH_PAGE_SIZE;
    }

    infra::MemoryRange<const FlashCommands::Command> FlashCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus FlashCommands::Info(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, infra::MakeRange(driverKeys)))
            return HilStatus::usage;

        Request candidate;
        auto status = ParseDriver(arguments, candidate);
        if (status == HilStatus::done)
            status = CheckDriver(candidate);
        if (status != HilStatus::done)
            return status;

        const auto geometry = LayoutGeometry(candidate.layout);
        infra::BigEndian<uint32_t> base{ Address(region.begin()) };
        auto line = context.response.Ok();
        line << " base=0x";
        line.Hex(infra::MakeByteRange(base));
        line << " sectors=" << geometry.sectors << " size=" << RegionSize() << " first=" << geometry.first << " image=" << imageEndPage << " layout=" << (candidate.layout == Layout::table ? "table" : "homogeneous");
        return HilStatus::done;
    }

    HilStatus FlashCommands::Erase(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, infra::MakeRange(driverKeys)))
            return HilStatus::usage;

        Request candidate;
        candidate.job = Job::erase;
        auto status = HilStatus::done;
        arguments.NumberAt(0, candidate.begin, 0, std::numeric_limits<uint32_t>::max(), status);
        arguments.NumberAt(1, candidate.end, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status == HilStatus::done)
            status = ParseDriver(arguments, candidate);
        if (status != HilStatus::done)
            return status;

        if (!usable)
            return HilStatus::unsupported;

        const auto geometry = LayoutGeometry(candidate.layout);
        if (candidate.begin < geometry.first || candidate.begin >= candidate.end || candidate.end > geometry.sectors)
            return HilStatus::range;

        status = CheckDriver(candidate);
        if (status == HilStatus::done)
            status = Reserve(candidate);
        if (status != HilStatus::done)
            return status;

        return Run(candidate);
    }

    HilStatus FlashCommands::Write(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, infra::MakeRange(writeKeys)))
            return HilStatus::usage;

        Request candidate;
        candidate.job = Job::write;
        auto status = HilStatus::done;
        arguments.NumberAt(0, candidate.address, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status == HilStatus::done)
            status = ParseDriver(arguments, candidate);
        if (status != HilStatus::done)
            return status;

        // An operation in flight may still program from the payload buffer
        if (pending.Busy())
            return HilStatus::busy;

        status = ParsePayload(arguments, 1, infra::MakeRange(buffer), payload);
        if (status == HilStatus::done && payload.empty())
            status = HilStatus::usage;
        if (status != HilStatus::done)
            return status;

        if (!usable)
            return HilStatus::unsupported;

        candidate.length = static_cast<uint32_t>(payload.size());
        const auto geometry = LayoutGeometry(candidate.layout);
        if (candidate.address < AddressOfSector(candidate.layout, geometry.first) || candidate.address > RegionSize() || candidate.length > RegionSize() - candidate.address)
            return HilStatus::range;

        status = CheckDriver(candidate);
        if (status == HilStatus::done)
            status = Reserve(candidate);
        if (status != HilStatus::done)
            return status;

        // Programming a flash word that is not erased fails the driver's assertion
        if (!Erased(candidate.address, candidate.length))
            return HilStatus::failed;

        return Run(candidate);
    }

    HilStatus FlashCommands::Read(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, infra::MakeRange(readKeys)))
            return HilStatus::usage;

        Request candidate;
        auto status = HilStatus::done;
        arguments.NumberAt(0, candidate.address, 0, std::numeric_limits<uint32_t>::max(), status);
        arguments.NumberAt(1, candidate.length, 1, std::numeric_limits<uint32_t>::max(), status);
        if (status == HilStatus::done)
            status = ParseOutput(arguments, candidate.output);
        if (status == HilStatus::done)
            status = ParseDriver(arguments, candidate);
        if (status != HilStatus::done)
            return status;

        if (!usable)
            return HilStatus::unsupported;

        if (candidate.address > RegionSize() || candidate.length > RegionSize() - candidate.address || CheckOutput(candidate.length, candidate.output) != HilStatus::done)
            return HilStatus::range;

        status = CheckDriver(candidate);
        if (status == HilStatus::done)
            status = Reserve(candidate);
        if (status != HilStatus::done)
            return status;

        return Run(candidate);
    }

#if defined(STM32WB)
    HilStatus FlashCommands::Stack(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(stackKeys)))
            return HilStatus::usage;

        auto state = StackState::stopped;
        Request candidate;
        candidate.variant = Variant::coord;
        auto status = HilStatus::done;
        arguments.SelectAt(0, state, stackStates, status);
        arguments.Select("layout", candidate.layout, layouts, status);
        if (status != HilStatus::done)
            return status;

        if (!usable)
            return HilStatus::unsupported;

        if (state != StackState::starting)
        {
            if (stackHeld)
            {
                // The driver has no way out of `starting` but FirmwareUpgradeServicesReady, which also runs the held step
                stackHeld = false;
                coordinated->FirmwareUpgradeServicesReady();
                if (operation == 0)
                    ReleaseAsynchronous();
            }

            context.response.Ok();
            return HilStatus::done;
        }

        if (stackHeld)
            return HilStatus::busy;

        status = Reserve(candidate);
        if (status != HilStatus::done)
            return status;

        request = candidate;
        status = ConstructAsynchronous();
        if (status != HilStatus::done)
            return status;

        coordinated->WirelessStackStarting();
        stackHeld = true;
        context.response.Ok();
        return HilStatus::done;
    }
#endif

    HilStatus FlashCommands::ParseDriver(const services::HilArguments& arguments, Request& candidate) const
    {
        auto status = HilStatus::done;
        arguments.Select("variant", candidate.variant, variants, status);
        arguments.Select("layout", candidate.layout, layouts, status);
        return status;
    }

    HilStatus FlashCommands::CheckDriver(const Request& candidate) const
    {
        if (!usable)
            return HilStatus::unsupported;

#if !defined(STM32WB)
        if (candidate.variant == Variant::coord)
            return HilStatus::unsupported;
#endif

        return HilStatus::done;
    }

    FlashCommands::Geometry FlashCommands::LayoutGeometry(Layout layout) const
    {
        const auto sectors = layout == Layout::table ? tableSectors : RegionSize() / FLASH_PAGE_SIZE;

        // Where the erase is unfixed (B.5) it takes the sector index for the absolute page; every sector starts at or
        // past the page of its index, so sectors from the image end page on cannot reach the running image
        // (board::imageLimitsErase)
        return { sectors, board::imageLimitsErase ? std::min(imageEndPage, sectors) : 0 };
    }

    uint32_t FlashCommands::AddressOfSector(Layout layout, uint32_t sector) const
    {
        if (layout == Layout::homogeneous)
            return sector * FLASH_PAGE_SIZE;

        uint32_t address = 0;
        for (uint32_t index = 0; index != sector; ++index)
            address += sectorSizes[index];

        return address;
    }

    uint32_t FlashCommands::RegionSize() const
    {
        return static_cast<uint32_t>(region.size());
    }

    bool FlashCommands::Erased(uint32_t start, uint32_t length) const
    {
        const auto first = start / flashWord * flashWord;
        const auto last = std::min((start + length + flashWord - 1) / flashWord * flashWord, RegionSize());

        return std::all_of(region.begin() + first, region.begin() + last, [](uint8_t byte)
            {
                return byte == 0xff;
            });
    }

    HilStatus FlashCommands::Reserve(const Request& candidate)
    {
        if (candidate.job == Job::read && candidate.variant == Variant::sync)
            return HilStatus::done;

#if defined(STM32WB)
        if (stackHeld)
            return !pending.Busy() && candidate.variant == Variant::coord && candidate.layout == activeLayout ? HilStatus::done : HilStatus::busy;
#endif

        // A coordinated step waiting for HSEM 7 runs once the semaphore is released, also after its ERR timeout
        return pending.Busy() ? HilStatus::busy : HilStatus::done;
    }

    HilStatus FlashCommands::Run(const Request& candidate)
    {
        // A synchronous read may run while an operation is held, which keeps its own request
        if (candidate.variant == Variant::sync)
        {
            RunSynchronous(candidate);
            return HilStatus::done;
        }

        request = candidate;
        auto status = HilStatus::done;
#if defined(STM32WB)
        if (!stackHeld)
            status = ConstructAsynchronous();
#else
        status = ConstructAsynchronous();
#endif
        if (status != HilStatus::done)
            return status;

        StartAsynchronous();
        return HilStatus::done;
    }

    void FlashCommands::RunSynchronous(const Request& synchronous)
    {
        if (synchronous.layout == Layout::table)
        {
            hal::SynchronousFlashInternalStm flash{ infra::MemoryRange<uint32_t>(sectorSizes.data(), sectorSizes.data() + tableSectors), region };
            RunSynchronous(flash, synchronous);
        }
        else
        {
            hal::SynchronousFlashHomogeneousInternalStm flash{ RegionSize() / FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, region };
            RunSynchronous(flash, synchronous);
        }
    }

    void FlashCommands::RunSynchronous(hal::SynchronousFlash& flash, const Request& synchronous)
    {
        if (synchronous.job == Job::read)
        {
            ReadSynchronous(flash, synchronous);
            return;
        }

        stopwatch.Start();
        if (synchronous.job == Job::erase)
            flash.EraseSectors(synchronous.begin, synchronous.end);
        else
            flash.WriteBuffer(payload, synchronous.address);

        context.response.Ok() << " us=" << stopwatch.ElapsedUs();
    }

    void FlashCommands::ReadSynchronous(hal::SynchronousFlash& flash, const Request& synchronous)
    {
        // A held coordinated write still programs from `buffer`, so a synchronous read streams through a small chunk
        std::array<uint8_t, readChunk> chunk;
        infra::Crc32 checksum;
        auto line = context.response.Ok();

        if (synchronous.output == Output::hex)
            line << " data=";

        for (uint32_t offset = 0; offset != synchronous.length;)
        {
            auto part = infra::Head(infra::MakeRange(chunk), std::min<std::size_t>(synchronous.length - offset, chunk.size()));
            flash.ReadBuffer(part, synchronous.address + offset);
            offset += static_cast<uint32_t>(part.size());

            if (synchronous.output == Output::hex)
                line.Hex(part);
            else
                checksum.Update(part);
        }

        if (synchronous.output == Output::crc)
            PrintCrc(line, synchronous.length, checksum.Result());
    }

    HilStatus FlashCommands::ConstructAsynchronous()
    {
        ReleaseAsynchronous();

#if defined(STM32WB)
        if (request.variant == Variant::coord)
        {
            auto status = resources.Claim(Resource::hsem, 0, coordinatedOwner);
            if (status != HilStatus::done)
                return status;

            HsemMaster();
        }
#endif

        activeLayout = request.layout;

        hal::FlashInternalStmBase* base = nullptr;
        if (request.layout == Layout::table)
            base = &asyncFlash.emplace<hal::FlashInternalStm>(infra::MemoryRange<const uint32_t>(sectorSizes.data(), sectorSizes.data() + tableSectors), region);
        else
            base = &asyncFlash.emplace<hal::FlashHomogeneousInternalStm>(RegionSize() / FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, region);

        activeFlash = base;
#if defined(STM32WB)
        if (request.variant == Variant::coord)
            activeFlash = &coordinated.emplace(*base, watchDogFactory.Borrow(), hal::FlashCoordinatedWithWirelessStack::WirelessStack::stopped);
#endif

        return HilStatus::done;
    }

    void FlashCommands::StartAsynchronous()
    {
        const auto timeout = request.job == Job::erase ? eraseTimeout : transferTimeout;
        operation = pending.Start(timeout);
        stopwatch.Start();

        switch (request.job)
        {
            case Job::erase:
                activeFlash->EraseSectors(request.begin, request.end, [this, doneOperation = operation]()
                    {
                        Done(doneOperation);
                    });
                break;
            case Job::write:
                activeFlash->WriteBuffer(payload, request.address, [this, doneOperation = operation]()
                    {
                        Done(doneOperation);
                    });
                break;
            case Job::read:
                crc.Reset();
                address = request.address;
                remaining = request.length;
                ReadNextChunk();
                break;
        }
    }

    void FlashCommands::ReadNextChunk()
    {
        auto chunk = infra::Head(infra::MakeRange(buffer), std::min<std::size_t>(remaining, buffer.size()));
        activeFlash->ReadBuffer(chunk, address, [this, chunkOperation = operation]()
            {
                ChunkRead(chunkOperation);
            });
    }

    void FlashCommands::ChunkRead(uint32_t chunkOperation)
    {
        if (chunkOperation != operation)
            return;

        const auto size = static_cast<uint32_t>(std::min<std::size_t>(remaining, buffer.size()));
        if (request.output == Output::crc)
            crc.Update(infra::Head(infra::MakeRange(buffer), size));

        address += size;
        remaining -= size;

        if (remaining != 0)
            ReadNextChunk();
        else
            Done(chunkOperation);
    }

    void FlashCommands::Done(uint32_t doneOperation)
    {
        if (doneOperation != operation)
            return;

        operation = 0;
        elapsed = stopwatch.ElapsedUs();

        // The driver calls this from its own scheduled callback; destroy it one event-loop turn later
        infra::EventDispatcher::Instance().Schedule([this]()
            {
#if defined(STM32WB)
                if (stackHeld)
                    return;
#endif
                if (operation == 0)
                    ReleaseAsynchronous();
            });

        if (pending.Complete(doneOperation))
        {
            Finish();
            return;
        }

        const char* job = "read";
        if (request.job == Job::erase)
            job = "erase";
        else if (request.job == Job::write)
            job = "write";

        context.response.Event("flash") << " op=" << job << " us=" << elapsed;
    }

    void FlashCommands::Finish()
    {
        auto line = context.response.Ok();

        if (request.job != Job::read)
            line << " us=" << elapsed;
        else if (request.output == Output::hex)
            PrintData(line, infra::Head(infra::MakeRange(buffer), request.length), Output::hex);
        else
            PrintCrc(line, request.length, crc.Result());
    }

    void FlashCommands::ReleaseAsynchronous()
    {
#if defined(STM32WB)
        if (coordinated)
        {
            coordinated.reset();
            watchDogFactory.Return();
            resources.Release(Resource::hsem, 0, coordinatedOwner);
        }
#endif

        asyncFlash.emplace<std::monostate>();
        activeFlash = nullptr;
    }
}
