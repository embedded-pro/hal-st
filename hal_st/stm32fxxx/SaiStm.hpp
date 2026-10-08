#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/AudioFormat.hpp"
#include "hal_st/stm32fxxx/AudioClockSwitchStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_SAI) && defined(HAL_SAI_MODULE_ENABLED)

namespace hal
{
    class SaiStm
    {
    public:
        explicit SaiStm(uint8_t oneBasedIndex);
        SaiStm(const SaiStm& other) = delete;
        SaiStm& operator=(const SaiStm& other) = delete;
        ~SaiStm();

        uint8_t OneBasedIndex() const;

    private:
        uint8_t oneBasedIndex;
    };

    class SaiBlockStm
    {
    public:
        struct Config
        {
            enum class Block : uint8_t
            {
                a,
                b
            };

            enum class Role : uint8_t
            {
                master,
                slave
            };

            enum class Synchronization : uint8_t
            {
                asynchronous,
                synchronousToOtherBlock
            };

            enum class KernelClock : uint8_t
            {
                pllI2s,
                pllSai
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

            Block block{ Block::a };
            Role role{ Role::master };
            Synchronization synchronization{ Synchronization::asynchronous };
            bool mclkOutput{ true };
            uint16_t maxRateErrorPermille{ 10 };
            KernelClock kernelClock{ KernelClock::pllI2s };
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

        SaiBlockStm(SaiStm& sai, Direction direction, const Config& config, GpioPinStm& sd, GpioPinStm& sck, GpioPinStm& fs, GpioPinStm& mclk);
        SaiBlockStm(const SaiBlockStm& other) = delete;
        SaiBlockStm& operator=(const SaiBlockStm& other) = delete;
        ~SaiBlockStm() = default;

        volatile void* DataRegister() const;
        void Configure(AudioFormat format);
        void StartPeripheral();
        void StopPeripheral();
        bool Pdm() const;

    private:
        uint32_t KernelClockFrequency();
        SAI_Block_TypeDef* Block() const;
        SAI_Block_TypeDef* OtherBlock() const;

    private:
        uint8_t oneBasedIndex;
        Direction direction;
        Config config;
        PeripheralPinStm sd;
        PeripheralPinStm sck;
        PeripheralPinStm fs;
        PeripheralPinStm mclk;
        SAI_HandleTypeDef handle{};
        uint32_t actualSampleRate{ 0 };
    };
}

#endif
