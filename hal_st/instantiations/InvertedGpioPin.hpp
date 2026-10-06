#pragma once

#include "hal/interfaces/Gpio.hpp"

namespace main_
{
    class InvertedGpioPin
        : public hal::GpioPin
    {
    public:
        explicit InvertedGpioPin(hal::GpioPin& pin)
            : pin(pin)
        {}

        bool Get() const override
        {
            return !pin.Get();
        }

        void Set(bool value) override
        {
            pin.Set(!value);
        }

        bool GetOutputLatch() const override
        {
            return !pin.GetOutputLatch();
        }

        void SetAsInput() override
        {
            pin.SetAsInput();
        }

        bool IsInput() const override
        {
            return pin.IsInput();
        }

        void Config(hal::PinConfigType config) override
        {
            Config(config, false);
        }

        void Config(hal::PinConfigType config, bool startOutputState) override
        {
            pin.Config(config, !startOutputState);
        }

        void ResetConfig() override
        {
            pin.ResetConfig();
        }

        void EnableInterrupt(const infra::Function<void()>& action, hal::InterruptTrigger trigger, hal::InterruptType type = hal::InterruptType::dispatched) override
        {
            pin.EnableInterrupt(action, Invert(trigger), type);
        }

        void DisableInterrupt() override
        {
            pin.DisableInterrupt();
        }

    private:
        static hal::InterruptTrigger Invert(hal::InterruptTrigger trigger)
        {
            switch (trigger)
            {
                case hal::InterruptTrigger::risingEdge:
                    return hal::InterruptTrigger::fallingEdge;
                case hal::InterruptTrigger::fallingEdge:
                    return hal::InterruptTrigger::risingEdge;
                default:
                    return hal::InterruptTrigger::bothEdges;
            }
        }

        hal::GpioPin& pin;
    };
}
