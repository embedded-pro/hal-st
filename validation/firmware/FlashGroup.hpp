#pragma once

#include "hal_st/stm32fxxx/FlashInternalStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousFlashInternalStm.hpp"
#include "infra/util/Crc.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "validation/firmware/Payload.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include "validation/firmware/WatchDogFactory.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#if defined(STM32WB)
#include "hal_st/stm32fxxx/FlashCoordinatedWithWirelessStack.hpp"
#endif

namespace validation
{
    class FlashCommands
        : public services::TerminalCommands
    {
    public:
        enum class Variant : uint8_t
        {
            sync,
            async,
            coord,
        };

        enum class Layout : uint8_t
        {
            homogeneous,
            table,
        };

        FlashCommands(services::HilContext& context, WatchDogFactoryStm& watchDogFactory, ResourceAllocation& resources);

        infra::MemoryRange<const Command> Commands() override;

    private:
        enum class Job : uint8_t
        {
            erase,
            write,
            read,
        };

        struct Request
        {
            Job job = Job::read;
            Variant variant = Variant::sync;
            Layout layout = Layout::homogeneous;
            uint32_t begin = 0;
            uint32_t end = 0;
            uint32_t address = 0;
            uint32_t length = 0;
            Output output = Output::hex;
        };

        struct Geometry
        {
            uint32_t sectors = 0;
            uint32_t first = 0;
        };

        services::HilStatus Info(const services::HilArguments& arguments);
        services::HilStatus Erase(const services::HilArguments& arguments);
        services::HilStatus Write(const services::HilArguments& arguments);
        services::HilStatus Read(const services::HilArguments& arguments);
#if defined(STM32WB)
        services::HilStatus Stack(const services::HilArguments& arguments);
#endif

        services::HilStatus ParseDriver(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus CheckDriver(const Request& request) const;
        Geometry LayoutGeometry(Layout layout) const;
        uint32_t AddressOfSector(Layout layout, uint32_t sector) const;
        uint32_t RegionSize() const;
        bool Erased(uint32_t address, uint32_t length) const;
        services::HilStatus Reserve(const Request& request);
        services::HilStatus Run(const Request& request);

        void RunSynchronous(const Request& synchronous);
        void RunSynchronous(hal::SynchronousFlash& flash, const Request& synchronous);
        void ReadSynchronous(hal::SynchronousFlash& flash, const Request& synchronous);
        services::HilStatus ConstructAsynchronous();
        void StartAsynchronous();
        void ReadNextChunk();
        void ChunkRead(uint32_t chunkOperation);
        void Done(uint32_t doneOperation);
        void Finish();
        void ReleaseAsynchronous();

    private:
        services::HilContext& context;
        WatchDogFactoryStm& watchDogFactory;
        ResourceAllocation& resources;
        services::HilPendingOperation pending;
        infra::ConstByteRange region;
        uint32_t imageEndPage = 0;
        bool usable = false;
        std::array<uint32_t, 80> sectorSizes{};
        uint32_t tableSectors = 0;

        std::variant<std::monostate, hal::FlashHomogeneousInternalStm, hal::FlashInternalStm> asyncFlash;
#if defined(STM32WB)
        std::optional<hal::FlashCoordinatedWithWirelessStack> coordinated;
        bool stackHeld = false;
#endif
        hal::Flash* activeFlash = nullptr;
        Layout activeLayout = Layout::homogeneous;

        Request request;
        Stopwatch stopwatch;
        std::array<uint8_t, 512> buffer{};
        infra::ByteRange payload;
        infra::Crc32 crc;
        uint32_t address = 0;
        uint32_t remaining = 0;
        uint32_t elapsed = 0;
        uint32_t operation = 0;
#if defined(STM32WB)
        std::array<Command, 5> commands;
#else
        std::array<Command, 4> commands;
#endif
    };
}
