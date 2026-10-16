#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/SdmmcBlockDeviceStm.hpp"
#include <array>
#include <chrono>
#include <cstdint>

#if defined(HAS_PERIPHERAL_SD) && !defined(SD_EXTERNAL_DMA)

namespace hal
{
    class MmcStm
        : public SdmmcBlockDeviceStm<detail::MmcHal>
    {
    public:
        struct Config
            : SdmmcBlockDeviceStm<detail::MmcHal>::Config
        {
            constexpr Config()
            {}

            bool cache{ false };
        };

        struct Identification
        {
            uint8_t manufacturer{ 0 };
            uint8_t oem{ 0 };
            std::array<char, 6> product{};
            uint8_t revision{ 0 };
            uint32_t serialNumber{ 0 };
            uint8_t manufacturingDate{ 0 };
        };

        MmcStm(uint8_t oneBasedIndex, GpioPinStm& clock, GpioPinStm& command, GpioPinStm& data0, GpioPinStm& data1, GpioPinStm& data2, GpioPinStm& data3, const Config& config = Config());
        ~MmcStm();
        MmcStm(const MmcStm& other) = delete;
        MmcStm& operator=(const MmcStm& other) = delete;

        uint32_t EraseGroupBlocks() const;
        bool CacheEnabled() const;
        const Identification& DeviceIdentification() const;

    private:
        void InitSdmmc(const Config& config);
        void ReadIdentification();
        bool EnableCache(std::chrono::milliseconds timeout);

    private:
        PeripheralPinStm clock;
        PeripheralPinStm command;
        PeripheralPinStm data0;
        PeripheralPinStm data1;
        PeripheralPinStm data2;
        PeripheralPinStm data3;
        uint8_t oneBasedIndex;
        Identification identification;
    };
}

#endif
