#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/AudioFormat.hpp"
#include "hal_st/stm32fxxx/AudioClockSwitchStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_SPI) && defined(HAL_I2S_MODULE_ENABLED)

namespace hal
{
    class I2sPeripheralStm
    {
    public:
        struct Config
        {
            enum class Role : uint8_t
            {
                master,
                slave
            };

            enum class KernelClock : uint8_t
            {
                pll,
                external
            };

            enum class Mode : uint8_t
            {
                pcm,
                pdm
            };

            enum class SampleEdge : uint8_t
            {
                rising,
                falling
            };

            constexpr Config()
            {}

            Role role{ Role::master };
            bool mclkOutput{ false };
            uint16_t maxRateErrorPermille{ 10 };
            KernelClock kernelClock{ KernelClock::pll };
            Mode mode{ Mode::pcm };
            SampleEdge pdmSampleEdge{ SampleEdge::rising };
            AudioClockSwitchStm* audioClock{ nullptr };
        };

        static bool IsSupported(AudioFormat format);

        uint32_t ActualSampleRate() const;

    protected:
        enum class Direction : uint8_t
        {
            transmit,
            receive
        };

        I2sPeripheralStm(uint8_t oneBasedIndex, Direction direction, const Config& config, GpioPinStm& sd, GpioPinStm& ck, GpioPinStm& ws, GpioPinStm& mclk);
        I2sPeripheralStm(const I2sPeripheralStm& other) = delete;
        I2sPeripheralStm& operator=(const I2sPeripheralStm& other) = delete;
        ~I2sPeripheralStm();

        volatile void* DataRegister() const;
        void Configure(AudioFormat format);
        void StartPeripheral();
        void StopPeripheral();
        bool Pdm() const;

    private:
        uint32_t KernelClockFrequency() const;
        void SetDmaRequest(bool enabled);

    private:
        uint8_t oneBasedIndex;
        Direction direction;
        Config config;
        PeripheralPinStm sd;
        PeripheralPinStm ck;
        PeripheralPinStm ws;
        PeripheralPinStm mclk;
        I2S_HandleTypeDef handle{};
        uint32_t actualSampleRate{ 0 };
    };
}

#endif
