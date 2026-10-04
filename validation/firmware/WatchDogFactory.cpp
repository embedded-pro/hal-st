#include "validation/firmware/WatchDogFactory.hpp"
#include <array>
#include <chrono>
#include <cstddef>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr std::array<const char*, 3> startKeys{ { "timeout", "feed", "pin" } };
        constexpr services::HilOwner warningPinOwner = services::HilOwners::extension + 1;
        constexpr uint32_t maximumTimeoutMs = 30000;
        constexpr uint64_t ticksUntilEarlyWarning = WWDG_CR_T - WWDG_CR_T_6;
        constexpr uint64_t wwdgClockDivider = 4096;
        constexpr uint64_t millisecondsPerSecond = 1000;

        constexpr std::array<uint32_t, 8> prescalers{ {
            WWDG_PRESCALER_1,
            WWDG_PRESCALER_2,
            WWDG_PRESCALER_4,
            WWDG_PRESCALER_8,
            WWDG_PRESCALER_16,
            WWDG_PRESCALER_32,
            WWDG_PRESCALER_64,
            WWDG_PRESCALER_128,
        } };

        std::optional<uint32_t> SmallestPrescaler(uint32_t timeoutMs)
        {
            const uint64_t required = static_cast<uint64_t>(timeoutMs) * HAL_RCC_GetPCLK1Freq();

            for (std::size_t exponent = 0; exponent != prescalers.size(); ++exponent)
                if (ticksUntilEarlyWarning * wwdgClockDivider * (uint64_t{ 1 } << exponent) * millisecondsPerSecond >= required)
                    return prescalers[exponent];

            return std::nullopt;
        }
    }

    WatchDogFactoryStm::WarningToggle::WarningToggle(hal::Watchdog& watchDog, hal::GpioPin& pin)
        : watchDog(watchDog)
        , pin(pin)
    {}

    infra::Duration WatchDogFactoryStm::WarningToggle::EarlyWarningPeriod() const
    {
        return watchDog.EarlyWarningPeriod();
    }

    void WatchDogFactoryStm::WarningToggle::Start(const infra::Function<void()>& onEarlyWarning)
    {
        this->onEarlyWarning = onEarlyWarning;
        watchDog.Start([this]()
            {
                pin.Set(!pin.GetOutputLatch());
                this->onEarlyWarning();
            });
    }

    void WatchDogFactoryStm::WarningToggle::Refresh()
    {
        watchDog.Refresh();
    }

    WatchDogFactoryStm::WatchDogFactoryStm(const services::HilPinNaming& naming, services::HilPinPool& pins)
        : naming(naming)
        , warningPins(pins, warningPinOwner)
    {}

    uint8_t WatchDogFactoryStm::Instances() const
    {
        return 1;
    }

    infra::MemoryRange<const char* const> WatchDogFactoryStm::StartKeys() const
    {
        return infra::MakeRange(startKeys);
    }

    HilStatus WatchDogFactoryStm::Prepare(uint8_t, const services::HilArguments& arguments)
    {
        Request request;
        HilStatus status = Parse(arguments, request);
        if (status != HilStatus::done)
            return status;

        uint32_t prescaler = WWDG_PRESCALER_1;
        return Validate(request, prescaler);
    }

    HilStatus WatchDogFactoryStm::Create(uint8_t, infra::Duration timeout, const services::HilArguments& arguments, hal::Watchdog*& created)
    {
        Request request;
        HilStatus status = Parse(arguments, request);
        request.timeoutMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());

        uint32_t prescaler = WWDG_PRESCALER_1;
        if (status == HilStatus::done)
            status = Validate(request, prescaler);
        if (status != HilStatus::done)
            return status;

        hal::GpioPin* gpio = nullptr;
        if (request.pin)
        {
            status = warningPins.Claim(*request.pin, services::HilPinPool::Use::exclusive, gpio);
            if (status != HilStatus::done)
                return status;

            gpio->Config(hal::PinConfigType::output, false);
        }

        hal::WatchDogStm::Config config;
        config.prescaler = prescaler;
        created = &watchDog.emplace(config);
        if (gpio != nullptr)
            created = &toggle.emplace(*watchDog, *gpio);

        return HilStatus::done;
    }

    HilStatus WatchDogFactoryStm::Parse(const services::HilArguments& arguments, Request& request) const
    {
        HilStatus status = HilStatus::done;
        arguments.Number("timeout", request.timeoutMs, 1, maximumTimeoutMs, status);
        arguments.Pin("pin", naming, request.pin, status);
        return status;
    }

    HilStatus WatchDogFactoryStm::Validate(const Request& request, uint32_t& prescaler) const
    {
        // A missing timeout is left to HilWatchDogCommands, which reports it as usage
        if (request.timeoutMs != 0)
        {
            const auto smallest = SmallestPrescaler(request.timeoutMs);
            if (!smallest)
                return HilStatus::range;

            prescaler = *smallest;
        }

        if (request.pin && !warningPins.Pool().Factory().IsValid(*request.pin))
            return HilStatus::pin;

        return HilStatus::done;
    }
}
