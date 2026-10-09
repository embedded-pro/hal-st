#include "demo/stm32h757i_eval/Mfx.hpp"

namespace main_
{
    namespace
    {
        constexpr uint8_t registerId = 0x00;
        constexpr uint8_t registerSystemControl = 0x40;
        constexpr uint8_t registerGpioState1 = 0x10;
        constexpr uint8_t registerGpioDirection1 = 0x60;
        constexpr uint8_t registerGpioDirection2 = 0x61;
        constexpr uint8_t registerGpioType1 = 0x64;
        constexpr uint8_t registerGpioType2 = 0x65;
        constexpr uint8_t registerGpioPullUpDown1 = 0x68;
        constexpr uint8_t registerGpioPullUpDown2 = 0x69;
        constexpr uint8_t systemControlGpioEnable = 0x01;

        constexpr uint8_t joystickPins = 0x1f;
        constexpr uint8_t sdCardDetectPin = 0x80;

        constexpr std::array<uint8_t, 2> unconfiguredIds{ 0x00, 0xff };
    }

    Mfx::Mfx(hal::I2cMaster& i2c, const Config& config, const infra::Function<void(bool found)>& onInitialized, const infra::Function<void(uint16_t pins)>& onPinsChanged)
        : registerAccess(i2c, deviceAddress)
        , config(config)
        , onInitialized(onInitialized)
        , onPinsChanged(onPinsChanged)
        , attemptsLeft(config.attempts)
    {
        Identify();
    }

    uint8_t Mfx::Id() const
    {
        return id[0];
    }

    uint16_t Mfx::Pins() const
    {
        return pins;
    }

    void Mfx::Identify()
    {
        id[0] = 0;
        registerAccess.ReadRegister(registerId, infra::MakeRange(id), [this]()
            {
                IdentificationRead();
            });
    }

    // The expander restarts with the board reset and does not answer for a while
    void Mfx::IdentificationRead()
    {
        if (id[0] != unconfiguredIds[0] && id[0] != unconfiguredIds[1])
        {
            Configure();
            return;
        }

        if (--attemptsLeft == 0)
        {
            onInitialized(false);
            return;
        }

        retryTimer.Start(config.retryInterval, [this]()
            {
                Identify();
            });
    }

    void Mfx::Configure()
    {
        stepIndex = 0;
        ConfigureNext();
    }

    void Mfx::ConfigureNext()
    {
        static constexpr std::array<Step, 7> steps{ { { registerSystemControl, systemControlGpioEnable },
            { registerGpioDirection1, 0x00 },
            { registerGpioDirection2, 0x00 },
            { registerGpioType1, joystickPins },
            { registerGpioPullUpDown1, joystickPins },
            { registerGpioType2, sdCardDetectPin },
            { registerGpioPullUpDown2, sdCardDetectPin } } };

        if (stepIndex == steps.size())
        {
            pollTimer.Start(config.pollInterval, [this]()
                {
                    Poll();
                });
            onInitialized(true);
            return;
        }

        const Step step = steps[stepIndex++];
        value[0] = step.value;
        registerAccess.WriteRegister(step.address, infra::MakeConstRange(value), [this]()
            {
                ConfigureNext();
            });
    }

    void Mfx::Poll()
    {
        if (reading)
            return;

        reading = true;
        registerAccess.ReadRegister(registerGpioState1, infra::MakeRange(pinBytes), [this]()
            {
                PinsRead();
            });
    }

    void Mfx::PinsRead()
    {
        reading = false;

        const uint16_t state = static_cast<uint16_t>(pinBytes[0] | (pinBytes[1] << 8));

        if (state != pins)
        {
            pins = state;
            onPinsChanged(pins);
        }
    }
}
