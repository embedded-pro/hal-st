#pragma once

#include "hal/interfaces/I2cRegisterAccess.hpp"
#include "hal/interfaces/TouchScreen.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <chrono>
#include <cstdint>

namespace main_
{
    class Ft6x06
        : public hal::TouchScreen
    {
    public:
        enum class InitializationResult : uint8_t
        {
            success,
            deviceNotFound
        };

        struct Config
        {
            constexpr Config()
            {}

            infra::Duration pollInterval{ std::chrono::milliseconds(20) };
            hal::TouchScreenSize size{ 480, 800 };
        };

        static constexpr hal::I2cAddress deviceAddress{ 0x38 };

        Ft6x06(hal::I2cMaster& i2c, const Config& config, const infra::Function<void(InitializationResult)>& onInitialized);
        Ft6x06(const Ft6x06& other) = delete;
        Ft6x06& operator=(const Ft6x06& other) = delete;

        hal::TouchScreenSize Size() const override;
        void Start(const infra::Function<void(Event event)>& onTouch) override;
        void Stop(const infra::Function<void()>& onStopped) override;

        uint8_t VendorId() const;
        uint8_t ChipId() const;

    private:
        void Poll();
        void SampleRead();
        void ReportStoppedWhenIdle();

    private:
        hal::I2cMasterRegisterAccessByte registerAccess;
        Config config;
        infra::Function<void(InitializationResult)> onInitialized;
        std::array<uint8_t, 2> identification{};
        std::array<uint8_t, 5> sample{};
        infra::TimerRepeating pollTimer;
        infra::Function<void(Event)> onTouch;
        infra::AutoResetFunction<void()> stopped;
        bool running{ false };
        bool reading{ false };
        bool down{ false };
        hal::TouchPoint lastPoint{ 0, 0 };
    };
}
