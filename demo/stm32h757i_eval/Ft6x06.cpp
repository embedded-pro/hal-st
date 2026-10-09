#include "demo/stm32h757i_eval/Ft6x06.hpp"
#include "infra/event/EventDispatcher.hpp"

namespace main_
{
    namespace
    {
        constexpr uint8_t registerTouchStatus = 0x02;
        constexpr uint8_t registerChipId = 0xa3;
        constexpr uint8_t registerVendorId = 0xa8;
        constexpr uint8_t touchCountMask = 0x0f;
        constexpr uint8_t coordinateHighMask = 0x0f;
        constexpr uint8_t maxTouches = 2;
    }

    Ft6x06::Ft6x06(hal::I2cMaster& i2c, const Config& config, const infra::Function<void(InitializationResult)>& onInitialized)
        : registerAccess(i2c, deviceAddress)
        , config(config)
        , onInitialized(onInitialized)
    {
        registerAccess.ReadRegister(registerVendorId, infra::Head(infra::MakeRange(identification), 1), [this]()
            {
                registerAccess.ReadRegister(registerChipId, infra::Tail(infra::MakeRange(identification), 1), [this]()
                    {
                        const bool present = identification[0] != 0x00 && identification[0] != 0xff;
                        this->onInitialized(present ? InitializationResult::success : InitializationResult::deviceNotFound);
                    });
            });
    }

    hal::TouchScreenSize Ft6x06::Size() const
    {
        return config.size;
    }

    void Ft6x06::Start(const infra::Function<void(Event event)>& onTouch)
    {
        this->onTouch = onTouch;
        running = true;
        down = false;

        pollTimer.Start(config.pollInterval, [this]()
            {
                Poll();
            });
    }

    void Ft6x06::Stop(const infra::Function<void()>& onStopped)
    {
        running = false;
        pollTimer.Cancel();
        stopped = onStopped;
        ReportStoppedWhenIdle();
    }

    uint8_t Ft6x06::VendorId() const
    {
        return identification[0];
    }

    uint8_t Ft6x06::ChipId() const
    {
        return identification[1];
    }

    void Ft6x06::Poll()
    {
        if (reading)
            return;

        reading = true;
        registerAccess.ReadRegister(registerTouchStatus, infra::MakeRange(sample), [this]()
            {
                SampleRead();
            });
    }

    void Ft6x06::SampleRead()
    {
        reading = false;

        if (!running)
        {
            ReportStoppedWhenIdle();
            return;
        }

        const uint8_t touches = sample[0] & touchCountMask;

        if (touches != 0 && touches <= maxTouches)
        {
            const hal::TouchPoint point{ static_cast<uint16_t>(((sample[1] & coordinateHighMask) << 8) | sample[2]),
                static_cast<uint16_t>(((sample[3] & coordinateHighMask) << 8) | sample[4]) };

            if (!down)
                onTouch(Event{ Phase::pressed, point });
            else if (!(point == lastPoint))
                onTouch(Event{ Phase::moved, point });

            down = true;
            lastPoint = point;
        }
        else if (touches == 0 && down)
        {
            down = false;
            onTouch(Event{ Phase::released, lastPoint });
        }
    }

    void Ft6x06::ReportStoppedWhenIdle()
    {
        if (!reading && stopped)
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    stopped();
                });
    }
}
