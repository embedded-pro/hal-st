#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include <array>
#include <cstdint>
#include <utility>

namespace main_
{
    constexpr uint32_t discoverySdRamBase = 0xd0000000;
    // The MT48LC4M32B2 has 16 MB, but only its lower 16 data lines are connected
    constexpr uint32_t discoverySdRamSize = 8 * 1024 * 1024;

    // A0 to A11, BA0, BA1, D0 to D15, NBL0, NBL1 and the bank 2 controls SDNWE, SDNRAS, SDNCAS, SDNE1, SDCKE1 and SDCLK
    inline constexpr std::array<std::pair<hal::Port, uint8_t>, 38> discoveryFmcPins = { { { hal::Port::F, 0 }, { hal::Port::F, 1 }, { hal::Port::F, 2 }, { hal::Port::F, 3 },
        { hal::Port::F, 4 }, { hal::Port::F, 5 }, { hal::Port::F, 12 }, { hal::Port::F, 13 },
        { hal::Port::F, 14 }, { hal::Port::F, 15 }, { hal::Port::G, 0 }, { hal::Port::G, 1 },
        { hal::Port::G, 4 }, { hal::Port::G, 5 }, { hal::Port::D, 14 }, { hal::Port::D, 15 },
        { hal::Port::D, 0 }, { hal::Port::D, 1 }, { hal::Port::E, 7 }, { hal::Port::E, 8 },
        { hal::Port::E, 9 }, { hal::Port::E, 10 }, { hal::Port::E, 11 }, { hal::Port::E, 12 },
        { hal::Port::E, 13 }, { hal::Port::E, 14 }, { hal::Port::E, 15 }, { hal::Port::D, 8 },
        { hal::Port::D, 9 }, { hal::Port::D, 10 }, { hal::Port::E, 0 }, { hal::Port::E, 1 },
        { hal::Port::H, 5 }, { hal::Port::F, 11 }, { hal::Port::G, 15 }, { hal::Port::H, 6 },
        { hal::Port::H, 7 }, { hal::Port::G, 8 } } };

    constexpr hal::SdRamStm::Config DiscoverySdRamConfig()
    {
        hal::SdRamStm::Config config{ discoverySdRamBase, discoverySdRamSize, 0x0603, 2, 8, 12, 16, 3 };
        config.readBurst = true;
        return config;
    }
}
