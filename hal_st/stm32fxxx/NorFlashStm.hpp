#pragma once

#include "hal/interfaces/Flash.hpp"
#include "hal/interfaces/Gpio.hpp"
#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

namespace hal
{
    class NorFlashStm
        : public hal::Flash
    {
    public:
        static constexpr uint32_t maxWriteBufferBytes = 1024;

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
            uint32_t writeBufferBytes{ 64 };
            FmcStm::NorSramBus bus;
            std::chrono::milliseconds programTimeout{ 1000 };
            std::chrono::milliseconds eraseTimeout{ 10000 };
            std::chrono::milliseconds pollInterval{ 1 };
        };

        NorFlashStm(FmcStm& fmc, const Config& config);
        NorFlashStm(FmcStm& fmc, GpioPin& readyBusy, const Config& config);
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
        void Begin(const infra::Function<void()>& onDone);
        void ProgramNextPage();
        void EraseNextSector();
        void WaitUntilReady(uint32_t offset, std::chrono::milliseconds timeout, const infra::Function<void()>& onReady);
        void CheckReady();
        void Finish();

    private:
        NOR_HandleTypeDef handle{};
        GpioPin* readyBusy = nullptr;
        uint32_t windowBase;
        uint32_t numberOfSectors;
        uint32_t sizeOfSector;
        uint32_t writeBufferBytes;
        std::chrono::milliseconds programTimeout;
        std::chrono::milliseconds eraseTimeout;
        std::chrono::milliseconds pollInterval;

        infra::ConstByteRange data;
        uint32_t address = 0;
        uint32_t sector = 0;
        uint32_t endSector = 0;
        std::array<uint16_t, maxWriteBufferBytes / 2> page{};

        uint32_t statusAddress = 0;
        infra::TimePoint deadline;
        infra::Function<void()> onReady;
        infra::TimerRepeating pollTimer;
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
