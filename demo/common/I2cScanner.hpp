#pragma once

#include "hal/interfaces/I2c.hpp"
#include <array>
#include <cstdint>

namespace main_
{
    // Reads one byte from every 7-bit address in turn and traces the ones that acknowledge
    class I2cScanner
    {
    public:
        explicit I2cScanner(hal::I2cMaster& i2c);

        void Scan();

    private:
        static constexpr uint16_t firstAddress = 0x08;
        static constexpr uint16_t lastAddress = 0x77;

        void Probe();

    private:
        hal::I2cMaster& i2c;
        std::array<uint8_t, 1> data{};
        uint16_t address{ firstAddress };
        uint32_t found{ 0 };
        bool scanning{ false };
    };
}
