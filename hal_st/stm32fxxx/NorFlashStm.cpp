#include "hal_st/stm32fxxx/NorFlashStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

namespace hal
{
    NorFlashStm::NorFlashStm(FmcStm&, const Config& config)
        : windowBase(FmcStm::NorSramWindow(config.oneBasedBank))
        , numberOfSectors(config.numberOfSectors)
        , sizeOfSector(config.sizeOfSector)
        , programTimeout(config.programTimeout)
        , eraseTimeout(config.eraseTimeout)
    {
        really_assert(config.bus.widthBits == 16);
        really_assert(sizeOfSector != 0 && numberOfSectors != 0);
        really_assert(static_cast<uint64_t>(numberOfSectors) * sizeOfSector <= FmcStm::norSramBankSize);

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
        HAL_NOR_DeInit(&handle);
#if defined(FMC_BCR1_FMCEN)
        __FMC_ENABLE();
#endif
    }

    NorFlashStm::Identification NorFlashStm::ReadIdentification()
    {
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
        really_assert(address + buffer.size() <= numberOfSectors * sizeOfSector);

        if (!buffer.empty() && (address & 1) != 0)
        {
            ProgramHalfWord(address - 1, static_cast<uint16_t>(0x00FF | (buffer.front() << 8)));
            ++address;
            buffer.pop_front();
        }

        while (buffer.size() >= 2)
        {
            ProgramHalfWord(address, static_cast<uint16_t>(buffer[0] | (buffer[1] << 8)));
            address += 2;
            buffer.pop_front(2);
        }

        if (!buffer.empty())
            ProgramHalfWord(address, static_cast<uint16_t>(0xFF00 | buffer.front()));

        auto result = HAL_NOR_ReturnToReadMode(&handle);
        really_assert(result == HAL_OK);

        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void NorFlashStm::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        really_assert(address + buffer.size() <= numberOfSectors * sizeOfSector);

        const auto* source = reinterpret_cast<const uint8_t*>(windowBase + address);
        std::copy(source, source + buffer.size(), buffer.begin());

        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void NorFlashStm::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        really_assert(beginIndex <= endIndex && endIndex <= numberOfSectors);

        for (uint32_t sector = beginIndex; sector != endIndex; ++sector)
        {
            auto result = HAL_NOR_Erase_Block(&handle, AddressOfSector(sector), windowBase);
            really_assert(result == HAL_OK);
            WaitForCompletion(windowBase + AddressOfSector(sector), eraseTimeout);
        }

        auto result = HAL_NOR_ReturnToReadMode(&handle);
        really_assert(result == HAL_OK);

        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void NorFlashStm::ProgramHalfWord(uint32_t address, uint16_t value)
    {
        if (value == 0xFFFF)
            return;

        auto* target = reinterpret_cast<uint32_t*>(windowBase + address);
        auto result = HAL_NOR_Program(&handle, target, &value);
        really_assert(result == HAL_OK);
        WaitForCompletion(windowBase + address, programTimeout);
    }

    void NorFlashStm::WaitForCompletion(uint32_t address, std::chrono::milliseconds timeout)
    {
        auto status = HAL_NOR_GetStatus(&handle, address, static_cast<uint32_t>(timeout.count()));
        really_assert(status == HAL_NOR_STATUS_SUCCESS);
    }
}

#endif
