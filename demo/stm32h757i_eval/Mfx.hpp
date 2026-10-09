#pragma once

#include "hal/interfaces/I2cRegisterAccess.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace main_
{
    // The multi-function expander (STM32L152 running ST's MFX firmware) of the MB1246, polled for its sixteen GPIOs
    class Mfx
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            infra::Duration pollInterval{ std::chrono::milliseconds(30) };
            infra::Duration retryInterval{ std::chrono::milliseconds(50) };
            uint8_t attempts{ 20 };
        };

        static constexpr hal::I2cAddress deviceAddress{ 0x42 };

        Mfx(hal::I2cMaster& i2c, const Config& config, const infra::Function<void(bool found)>& onInitialized, const infra::Function<void(uint16_t pins)>& onPinsChanged);
        Mfx(const Mfx& other) = delete;
        Mfx& operator=(const Mfx& other) = delete;

        uint8_t Id() const;
        uint16_t Pins() const;

    private:
        struct Step
        {
            uint8_t address;
            uint8_t value;
        };

        void Identify();
        void IdentificationRead();
        void Configure();
        void ConfigureNext();
        void Poll();
        void PinsRead();

    private:
        hal::I2cMasterRegisterAccessByte registerAccess;
        Config config;
        infra::Function<void(bool found)> onInitialized;
        infra::Function<void(uint16_t pins)> onPinsChanged;
        infra::TimerSingleShot retryTimer;
        infra::TimerRepeating pollTimer;
        std::array<uint8_t, 1> id{};
        std::array<uint8_t, 2> pinBytes{};
        std::array<uint8_t, 1> value{};
        uint8_t attemptsLeft;
        std::size_t stepIndex{ 0 };
        uint16_t pins{ 0 };
        bool reading{ false };
    };
}
