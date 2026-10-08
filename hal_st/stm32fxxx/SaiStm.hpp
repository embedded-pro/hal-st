#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/AudioFormat.hpp"
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

            constexpr Config()
            {}

            Block block{ Block::a };
            Role role{ Role::master };
            Synchronization synchronization{ Synchronization::asynchronous };
            bool mclkOutput{ true };
            uint16_t maxRateErrorPermille{ 10 };
            KernelClock kernelClock{ KernelClock::pllI2s };
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

    private:
        uint32_t KernelClockFrequency();
        SAI_Block_TypeDef* Block() const;

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
