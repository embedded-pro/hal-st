#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/NorFlashStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "hal_st/stm32fxxx/SramStm.hpp"
#include <array>
#include <cstdint>
#include <utility>

namespace main_
{
    constexpr uint32_t evalSdRamBase = 0xd0000000;
    constexpr uint32_t evalSdRamSize = 32 * 1024 * 1024;
    constexpr uint32_t evalSramSize = 2 * 1024 * 1024;
    constexpr uint32_t evalNorBase = 0x60000000;
    constexpr uint32_t evalNorSectors = 128;
    constexpr uint32_t evalNorSectorSize = 128 * 1024;

    // A20 to A22 of the NOR flash (PE4 to PE6) and PE2 are not listed: on the MB1246 those pins are also SAI1 FS, SCK, SD and MCLK
    inline constexpr std::array<std::pair<hal::Port, uint8_t>, 66> evalFmcPins = { { { hal::Port::F, 0 }, { hal::Port::F, 1 }, { hal::Port::F, 2 }, { hal::Port::F, 3 },
        { hal::Port::F, 4 }, { hal::Port::F, 5 }, { hal::Port::F, 12 }, { hal::Port::F, 13 },
        { hal::Port::F, 14 }, { hal::Port::F, 15 }, { hal::Port::G, 0 }, { hal::Port::G, 1 },
        { hal::Port::G, 2 }, { hal::Port::G, 3 }, { hal::Port::G, 4 }, { hal::Port::G, 5 },
        { hal::Port::D, 11 }, { hal::Port::D, 12 }, { hal::Port::D, 13 }, { hal::Port::E, 3 },
        { hal::Port::D, 14 }, { hal::Port::D, 15 }, { hal::Port::D, 0 }, { hal::Port::D, 1 },
        { hal::Port::E, 7 }, { hal::Port::E, 8 }, { hal::Port::E, 9 }, { hal::Port::E, 10 },
        { hal::Port::E, 11 }, { hal::Port::E, 12 }, { hal::Port::E, 13 }, { hal::Port::E, 14 },
        { hal::Port::E, 15 }, { hal::Port::D, 8 }, { hal::Port::D, 9 }, { hal::Port::D, 10 },
        { hal::Port::H, 8 }, { hal::Port::H, 9 }, { hal::Port::H, 10 }, { hal::Port::H, 11 },
        { hal::Port::H, 12 }, { hal::Port::H, 13 }, { hal::Port::H, 14 }, { hal::Port::H, 15 },
        { hal::Port::I, 0 }, { hal::Port::I, 1 }, { hal::Port::I, 2 }, { hal::Port::I, 3 },
        { hal::Port::I, 6 }, { hal::Port::I, 7 }, { hal::Port::I, 9 }, { hal::Port::I, 10 },
        { hal::Port::E, 0 }, { hal::Port::E, 1 }, { hal::Port::I, 4 }, { hal::Port::I, 5 },
        { hal::Port::D, 4 }, { hal::Port::D, 5 }, { hal::Port::D, 7 }, { hal::Port::G, 10 },
        { hal::Port::G, 8 }, { hal::Port::H, 5 }, { hal::Port::H, 6 }, { hal::Port::H, 7 },
        { hal::Port::F, 11 }, { hal::Port::G, 15 } } };

    constexpr hal::SdRamStm::Config EvalSdRamConfig()
    {
        hal::SdRamStm::Config config{ evalSdRamBase, evalSdRamSize, 0x0603, 2, 9, 12, 32, 3 };
        config.timing.selfRefreshTime = 5;
        return config;
    }

    constexpr hal::SramStm::Config EvalSramConfig()
    {
        hal::SramStm::Config config;
        config.oneBasedBank = 3;
        config.size = evalSramSize;
        config.bus.timing.addressSetup = 2;
        config.bus.timing.addressHold = 1;
        config.bus.timing.dataSetup = 4;
        config.bus.timing.busTurnaround = 1;
        config.bus.timing.clockDivision = 2;
        config.bus.timing.dataLatency = 2;
        return config;
    }

    constexpr hal::NorFlashStm::Config EvalNorConfig()
    {
        hal::NorFlashStm::Config config;
        config.oneBasedBank = 1;
        config.numberOfSectors = evalNorSectors;
        config.sizeOfSector = evalNorSectorSize;
        config.bus.timing.addressSetup = 4;
        config.bus.timing.addressHold = 3;
        config.bus.timing.dataSetup = 20;
        config.bus.timing.busTurnaround = 2;
        config.bus.timing.clockDivision = 2;
        config.bus.timing.dataLatency = 2;
        return config;
    }
}
