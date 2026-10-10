#pragma once

#include "drivers/touch_screen/ft6x06/Ft6x06.hpp"
#include "drivers/touch_screen/ft6x06/Ft6x06BusAccessI2c.hpp"
#include "hal/interfaces/I2c.hpp"
#include "hal/interfaces/TouchScreen.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace main_
{
    // Identifies a FocalTech touch controller, reports what it found and then forwards every touch event, tracing the events too
    class TouchMonitor
    {
    public:
        TouchMonitor(hal::I2cMaster& i2c, const drivers::Ft6x06::Config& config, const infra::Function<void(bool found, uint8_t vendorId, uint8_t chipId)>& onIdentified, const infra::Function<void(const hal::TouchScreen::Event& event)>& onTouch);

        void Begin();

    private:
        static constexpr uint32_t movedEventsPerTrace = 10;

        void Initialized(drivers::Ft6x06::InitializationResult result);
        void Report(const hal::TouchScreen::Event& event);
        static const char* PhaseName(hal::TouchScreen::Phase phase);

    private:
        drivers::Ft6x06BusAccessI2c bus;
        drivers::Ft6x06::Config config;
        infra::Function<void(bool found, uint8_t vendorId, uint8_t chipId)> onIdentified;
        infra::Function<void(const hal::TouchScreen::Event& event)> onTouch;
        std::optional<drivers::Ft6x06> touch;
        uint32_t movedEvents = 0;
    };
}
