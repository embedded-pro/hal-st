#pragma once

#include "hal/interfaces/Flash.hpp"
#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <chrono>
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

namespace hal
{
    // The HAL speaks the AMD and Intel command sets over a 16-bit bus only, and keeps the bus width in a
    // file-static, so a single instance may exist at a time. Sectors are uniform. Program and erase block.
    class NorFlashStm
        : public hal::Flash
    {
    public:
        struct Identification
        {
            uint16_t manufacturer;
            uint16_t device1;
            uint16_t device2;
            uint16_t device3;
        };

        struct Config
        {
            constexpr Config()
            {}

            uint8_t oneBasedBank{ 1 };
            uint32_t numberOfSectors{ 0 };
            uint32_t sizeOfSector{ 0 };
            FmcStm::NorSramBus bus;
            std::chrono::milliseconds programTimeout{ 1000 };
            std::chrono::milliseconds eraseTimeout{ 10000 };
        };

        NorFlashStm(FmcStm& fmc, const Config& config);
        ~NorFlashStm();
        NorFlashStm(const NorFlashStm& other) = delete;
        NorFlashStm& operator=(const NorFlashStm& other) = delete;

        Identification ReadIdentification();

        uint32_t NumberOfSectors() const override;
        uint32_t SizeOfSector(uint32_t sectorIndex) const override;
        uint32_t SectorOfAddress(uint32_t address) const override;
        uint32_t AddressOfSector(uint32_t sectorIndex) const override;

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override;

    private:
        void ProgramHalfWord(uint32_t address, uint16_t value);
        void WaitForCompletion(uint32_t address, std::chrono::milliseconds timeout);

    private:
        NOR_HandleTypeDef handle{};
        uint32_t windowBase;
        uint32_t numberOfSectors;
        uint32_t sizeOfSector;
        std::chrono::milliseconds programTimeout;
        std::chrono::milliseconds eraseTimeout;
    };
}

#endif
