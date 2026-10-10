#include "demo/common/TouchMonitor.hpp"
#include "services/tracer/GlobalTracer.hpp"

namespace main_
{
    TouchMonitor::TouchMonitor(hal::I2cMaster& i2c, const drivers::Ft6x06::Config& config, const infra::Function<void(bool found, uint8_t vendorId, uint8_t chipId)>& onIdentified, const infra::Function<void(const hal::TouchScreen::Event& event)>& onTouch)
        : bus(i2c)
        , config(config)
        , onIdentified(onIdentified)
        , onTouch(onTouch)
    {}

    void TouchMonitor::Begin()
    {
        touch.emplace(bus, config, [this](drivers::Ft6x06::InitializationResult result)
            {
                Initialized(result);
            });
    }

    void TouchMonitor::Initialized(drivers::Ft6x06::InitializationResult result)
    {
        if (result != drivers::Ft6x06::InitializationResult::success)
        {
            services::GlobalTracer().Trace() << "touch controller not found at 0x38";
            onIdentified(false, 0, 0);
            return;
        }

        services::GlobalTracer().Trace() << "touch controller vendor " << infra::hex << static_cast<uint32_t>(touch->VendorId()) << ", chip " << static_cast<uint32_t>(touch->ChipId());
        onIdentified(true, touch->VendorId(), touch->ChipId());

        touch->Start([this](hal::TouchScreen::Event event)
            {
                Report(event);
            });
    }

    void TouchMonitor::Report(const hal::TouchScreen::Event& event)
    {
        onTouch(event);

        if (event.phase != hal::TouchScreen::Phase::moved)
            movedEvents = 0;
        else if (++movedEvents % movedEventsPerTrace != 0)
            return;

        services::GlobalTracer().Trace() << "touch " << PhaseName(event.phase) << " " << static_cast<uint32_t>(event.point.x) << "," << static_cast<uint32_t>(event.point.y);
    }

    const char* TouchMonitor::PhaseName(hal::TouchScreen::Phase phase)
    {
        switch (phase)
        {
            case hal::TouchScreen::Phase::pressed:
                return "pressed";
            case hal::TouchScreen::Phase::moved:
                return "moved";
            case hal::TouchScreen::Phase::released:
                return "released";
        }

        return "";
    }
}
