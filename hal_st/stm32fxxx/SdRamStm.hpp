#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_SDRAM) && defined(HAS_PERIPHERAL_FMC)

namespace hal
{
    class SdRamStm
    {
    public:
        struct Timing
        {
            constexpr Timing()
            {}

            uint8_t loadToActiveDelay{ 2 };
            uint8_t exitSelfRefreshDelay{ 7 };
            uint8_t selfRefreshTime{ 4 };
            uint8_t rowCycleDelay{ 7 };
            uint8_t writeRecoveryTime{ 2 };
            uint8_t rowPrechargeDelay{ 2 };
            uint8_t rowToColumnDelay{ 2 };
        };

        struct Config
        {
            constexpr Config()
            {}

            constexpr Config(uint32_t address, uint32_t size, uint32_t refreshCount, uint8_t bank, uint8_t columnBits, uint8_t rowBits, uint8_t busWidth, uint8_t casLatency)
                : address(address)
                , size(size)
                , refreshCount(refreshCount)
                , bank(bank)
                , columnBits(columnBits)
                , rowBits(rowBits)
                , busWidth(busWidth)
                , casLatency(casLatency)
            {}

            uint32_t address{ 0 };
            uint32_t size{ 0 };
            uint32_t refreshCount{ 0 };
            uint8_t bank{ 1 };
            uint8_t columnBits{ 8 };
            uint8_t rowBits{ 12 };
            uint8_t busWidth{ 16 };
            uint8_t casLatency{ 2 };
            uint8_t internalBanks{ 4 };
            uint8_t sdClockDivider{ 2 };
            bool readBurst{ false };
            uint8_t readPipeDelay{ 0 };
            uint8_t burstLength{ 1 };
            bool writeProtection{ false };
            uint8_t autoRefreshCycles{ 8 };
            std::chrono::microseconds powerUpDelay{ 1000 };
            Timing timing;
        };

        SdRamStm(FmcStm& fmc, const Config& config);
        SdRamStm(MultiGpioPinStm& sdramPins, const Config& config);
        ~SdRamStm();
        SdRamStm(const SdRamStm& other) = delete;
        SdRamStm& operator=(const SdRamStm& other) = delete;

        infra::ByteRange Memory() const;
        void SanityCheck();

    private:
        void Initialize(const Config& config);
        void SendCommand(uint32_t mode, uint32_t autoRefreshNumber, uint32_t modeRegister);

    private:
        std::optional<FmcStm> ownedFmc;
        SDRAM_HandleTypeDef handle{};
        infra::ByteRange memory;
        uint32_t commandTarget{ 0 };
    };

    inline constexpr SdRamStm::Config stm32f7discoverySdRamConfig = { 0xC0000000, 0x800000, 0x0603, 1, 8, 12, 16, 2 };
    inline constexpr std::array<std::pair<hal::Port, uint8_t>, 39> stm32f7discoveryFmcPins = { { { hal::Port::C, 3 }, { hal::Port::D, 0 }, { hal::Port::D, 1 }, { hal::Port::D, 3 },
        { hal::Port::D, 8 }, { hal::Port::D, 9 }, { hal::Port::D, 10 }, { hal::Port::D, 14 },
        { hal::Port::D, 15 }, { hal::Port::E, 0 }, { hal::Port::E, 1 }, { hal::Port::E, 7 },
        { hal::Port::E, 8 }, { hal::Port::E, 9 }, { hal::Port::E, 10 }, { hal::Port::E, 11 },
        { hal::Port::E, 12 }, { hal::Port::E, 13 }, { hal::Port::E, 14 }, { hal::Port::E, 15 },
        { hal::Port::F, 0 }, { hal::Port::F, 1 }, { hal::Port::F, 2 }, { hal::Port::F, 3 },
        { hal::Port::F, 4 }, { hal::Port::F, 5 }, { hal::Port::F, 11 }, { hal::Port::F, 12 },
        { hal::Port::F, 13 }, { hal::Port::F, 14 }, { hal::Port::F, 15 }, { hal::Port::G, 0 },
        { hal::Port::G, 1 }, { hal::Port::G, 4 }, { hal::Port::G, 5 }, { hal::Port::G, 8 },
        { hal::Port::G, 15 }, { hal::Port::H, 3 }, { hal::Port::H, 5 } } };

    inline constexpr SdRamStm::Config MakeStm32f429discoverySdRamConfig()
    {
        SdRamStm::Config config{ 0xD0000000, 0x800000, 1386, 2, 8, 12, 16, 3 };
        config.readPipeDelay = 1;
        return config;
    }

    inline constexpr SdRamStm::Config stm32f429discoverySdRamConfig = MakeStm32f429discoverySdRamConfig();
    inline constexpr std::array<std::pair<hal::Port, uint8_t>, 38> stm32f429discoveryFmcPins = { { { hal::Port::B, 5 }, { hal::Port::B, 6 }, { hal::Port::C, 0 }, { hal::Port::D, 0 },
        { hal::Port::D, 1 }, { hal::Port::D, 8 }, { hal::Port::D, 9 }, { hal::Port::D, 10 },
        { hal::Port::D, 14 }, { hal::Port::D, 15 }, { hal::Port::E, 0 }, { hal::Port::E, 1 },
        { hal::Port::E, 7 }, { hal::Port::E, 8 }, { hal::Port::E, 9 }, { hal::Port::E, 10 },
        { hal::Port::E, 11 }, { hal::Port::E, 12 }, { hal::Port::E, 13 }, { hal::Port::E, 14 },
        { hal::Port::E, 15 }, { hal::Port::F, 0 }, { hal::Port::F, 1 }, { hal::Port::F, 2 },
        { hal::Port::F, 3 }, { hal::Port::F, 4 }, { hal::Port::F, 5 }, { hal::Port::F, 11 },
        { hal::Port::F, 12 }, { hal::Port::F, 13 }, { hal::Port::F, 14 }, { hal::Port::F, 15 },
        { hal::Port::G, 0 }, { hal::Port::G, 1 }, { hal::Port::G, 4 }, { hal::Port::G, 5 },
        { hal::Port::G, 8 }, { hal::Port::G, 15 } } };
}

#endif
