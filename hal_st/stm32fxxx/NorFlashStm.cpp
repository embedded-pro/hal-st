#include "hal_st/stm32fxxx/NorFlashStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

namespace
{
    bool IsWithin(uint32_t address, std::size_t size, uint32_t total)
    {
        return address <= total && size <= total - address;
    }
}

namespace hal
{
    NorFlashStm::NorFlashStm(FmcStm&, const Config& config)
        : windowBase(FmcStm::NorSramWindow(config.oneBasedBank))
        , numberOfSectors(config.numberOfSectors)
        , sizeOfSector(config.sizeOfSector)
        , writeBufferBytes(config.writeBufferBytes)
        , programTimeout(config.programTimeout)
        , eraseTimeout(config.eraseTimeout)
    {
        really_assert(config.bus.widthBits == 16);
        really_assert(sizeOfSector != 0 && numberOfSectors != 0);
        really_assert(static_cast<uint64_t>(numberOfSectors) * sizeOfSector <= FmcStm::norSramBankSize);
        really_assert(writeBufferBytes >= 2 && writeBufferBytes <= maxWriteBufferBytes && (writeBufferBytes & (writeBufferBytes - 1)) == 0);

        handle.Instance = FMC_NORSRAM_DEVICE;
        handle.Extended = FMC_NORSRAM_EXTENDED_DEVICE;
        handle.Init = FmcStm::CreateNorSramInit(config.oneBasedBank, FmcStm::MemoryType::nor, true, config.bus);

        auto timing = FmcStm::CreateNorSramTiming(config.bus.timing);
        auto writeTiming = config.bus.writeTiming ? FmcStm::CreateNorSramTiming(*config.bus.writeTiming) : timing;

        auto result = HAL_NOR_Init(&handle, &timing, &writeTiming);
        really_assert(result == HAL_OK);
    }

    NorFlashStm::~NorFlashStm()
    {
        really_assert(!onDone);

        HAL_NOR_DeInit(&handle);
#if defined(FMC_BCR1_FMCEN)
        __FMC_ENABLE();
#endif
    }

    NorFlashStm::Identification NorFlashStm::ReadIdentification()
    {
        really_assert(!onDone);

        NOR_IDTypeDef id{};

        auto result = HAL_NOR_Read_ID(&handle, &id);
        really_assert(result == HAL_OK);
        result = HAL_NOR_ReturnToReadMode(&handle);
        really_assert(result == HAL_OK);

        return { id.Manufacturer_Code, id.Device_Code1, id.Device_Code2, id.Device_Code3 };
    }

    uint32_t NorFlashStm::NumberOfSectors() const
    {
        return numberOfSectors;
    }

    uint32_t NorFlashStm::SizeOfSector(uint32_t) const
    {
        return sizeOfSector;
    }

    uint32_t NorFlashStm::SectorOfAddress(uint32_t address) const
    {
        return address / sizeOfSector;
    }

    uint32_t NorFlashStm::AddressOfSector(uint32_t sectorIndex) const
    {
        return sectorIndex * sizeOfSector;
    }

    void NorFlashStm::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        really_assert(IsWithin(address, buffer.size(), numberOfSectors * sizeOfSector));
        Begin(onDone);

        data = buffer;
        this->address = address;
        ProgramNextPage();
    }

    void NorFlashStm::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        really_assert(IsWithin(address, buffer.size(), numberOfSectors * sizeOfSector));
        Begin(onDone);

        const auto* source = reinterpret_cast<const uint8_t*>(windowBase + address);
        std::copy(source, source + buffer.size(), buffer.begin());

        Finish();
    }

    void NorFlashStm::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        really_assert(beginIndex <= endIndex && endIndex <= numberOfSectors);
        Begin(onDone);

        sector = beginIndex;
        endSector = endIndex;
        EraseNextSector();
    }

    void NorFlashStm::Begin(const infra::Function<void()>& onDone)
    {
        really_assert(!this->onDone);
        this->onDone = onDone;
    }

    void NorFlashStm::ProgramNextPage()
    {
        if (data.empty())
        {
            Finish();
            return;
        }

        auto pageStart = address & ~1u;
        auto pageEnd = std::min<uint32_t>((address / writeBufferBytes + 1) * writeBufferBytes, address + data.size());
        auto words = (pageEnd - pageStart + 1) / 2;
        auto* bytes = reinterpret_cast<uint8_t*>(page.data());

        std::fill(bytes, bytes + words * 2, 0xff);
        std::copy(data.begin(), data.begin() + (pageEnd - address), bytes + (address - pageStart));
        data.pop_front(pageEnd - address);
        address = pageEnd;

        auto result = HAL_NOR_ProgramBuffer(&handle, pageStart, page.data(), words);
        really_assert(result == HAL_OK);
        WaitUntilReady(pageStart, programTimeout, [this]
            {
                ProgramNextPage();
            });
    }

    void NorFlashStm::EraseNextSector()
    {
        if (sector == endSector)
        {
            Finish();
            return;
        }

        auto result = HAL_NOR_Erase_Block(&handle, AddressOfSector(sector), windowBase);
        really_assert(result == HAL_OK);
        WaitUntilReady(AddressOfSector(sector), eraseTimeout, [this]
            {
                ++sector;
                EraseNextSector();
            });
    }

    void NorFlashStm::WaitUntilReady(uint32_t offset, std::chrono::milliseconds timeout, const infra::Function<void()>& onReady)
    {
        statusAddress = windowBase + offset;
        deadline = infra::Now() + timeout;
        this->onReady = onReady;
        ScheduleCheck();
    }

    void NorFlashStm::ScheduleCheck()
    {
        infra::EventDispatcher::Instance().Schedule([this]
            {
                CheckReady();
            });
    }

    void NorFlashStm::CheckReady()
    {
        auto status = HAL_NOR_GetStatus(&handle, statusAddress, 0);
        really_assert(status != HAL_NOR_STATUS_ERROR);

        if (status != HAL_NOR_STATUS_SUCCESS)
        {
            really_assert(infra::Now() <= deadline);
            ScheduleCheck();
            return;
        }

        auto continuation = onReady;
        continuation();
    }

    void NorFlashStm::Finish()
    {
        auto result = HAL_NOR_ReturnToReadMode(&handle);
        really_assert(result == HAL_OK);

        infra::EventDispatcher::Instance().Schedule([this]
            {
                onDone();
            });
    }
}

#endif
