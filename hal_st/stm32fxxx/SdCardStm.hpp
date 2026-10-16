#pragma once

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/SdmmcBlockDeviceStm.hpp"
#include <array>
#include <cstdint>
#include <optional>

#if defined(HAS_PERIPHERAL_SD)

namespace hal
{
    class SdCardStm
        : public SdmmcBlockDeviceStm<detail::SdCardHal>
    {
    public:
        enum class BusWidth : uint8_t
        {
            oneBit,
            fourBit
        };

        struct Config
            : SdmmcBlockDeviceStm<detail::SdCardHal>::Config
        {
            constexpr Config()
            {}

            BusWidth busWidth{ BusWidth::fourBit };
#if defined(SD_EXTERNAL_DMA)
            DmaChannelId dma{ 2, 3, 4 };
#endif
        };

        struct Identification
        {
            uint8_t manufacturer{ 0 };
            uint16_t oem{ 0 };
            std::array<char, 5> product{};
            uint8_t revision{ 0 };
            uint32_t serialNumber{ 0 };
            uint16_t manufacturingDate{ 0 };
        };

        struct DirectionPins
        {
            DirectionPins()
                : data0{ dummyPinStm }
                , data123{ dummyPinStm }
                , command{ dummyPinStm }
            {}

            DirectionPins(GpioPinStm& data0, GpioPinStm& data123, GpioPinStm& command)
                : data0{ data0 }
                , data123{ data123 }
                , command{ command }
            {}

            GpioPinStm& data0;
            GpioPinStm& data123;
            GpioPinStm& command;
        };

        SdCardStm(uint8_t oneBasedIndex, GpioPinStm& clock, GpioPinStm& command, GpioPinStm& data0, GpioPinStm& data1 = dummyPinStm, GpioPinStm& data2 = dummyPinStm, GpioPinStm& data3 = dummyPinStm, const Config& config = Config(), const DirectionPins& directionPins = DirectionPins{});
        ~SdCardStm();
        SdCardStm(const SdCardStm& other) = delete;
        SdCardStm& operator=(const SdCardStm& other) = delete;

        uint32_t AllocationUnitBlocks() const;
        const Identification& CardIdentification() const;

    private:
        void InitSdmmc(const Config& config);
        void ReadCardRegisters();
#if defined(SD_EXTERNAL_DMA)
        void InitDma(const Config& config);
#endif

    private:
        PeripheralPinStm clock;
        PeripheralPinStm command;
        PeripheralPinStm data0;
        PeripheralPinStm data1;
        PeripheralPinStm data2;
        PeripheralPinStm data3;
        PeripheralPinStm data0Direction;
        PeripheralPinStm data123Direction;
        PeripheralPinStm commandDirection;
        uint8_t oneBasedIndex;
#if defined(SD_EXTERNAL_DMA)
        DMA_HandleTypeDef dma{};
        bool dmaInitialized{ false };
        std::optional<cortex::DispatchedInterruptHandler> dmaInterrupt;
#endif
        uint32_t allocationUnitBlocks{ 0 };
        Identification identification;
    };
}

#endif
