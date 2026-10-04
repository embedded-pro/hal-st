#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "services/hil/HilPinId.hpp"
#include <array>
#include <cstdint>

namespace validation
{
    using services::HilPinAlias;
    using services::HilPinId;

    constexpr HilPinId Pin(hal::Port port, uint8_t index)
    {
        return HilPinId{ static_cast<uint8_t>(port), index };
    }

    constexpr hal::Port PortOf(HilPinId pin)
    {
        return static_cast<hal::Port>(pin.port);
    }

    struct UartPins
    {
        uint8_t index;
        bool lpuart;
        HilPinId tx;
        HilPinId rx;
    };

    struct QeiPins
    {
        uint8_t timer;
        HilPinId a;
        HilPinId b;
        HilPinId idx;
    };

    struct DmaRequests
    {
        uint8_t transmit;
        uint8_t receive;
    };

    struct DmaChannel
    {
        uint8_t dma;
        uint8_t channel;
    };

    struct DmaPair
    {
        DmaChannel transmit;
        DmaChannel receive;
    };

    struct I2cPins
    {
        uint8_t index;
        HilPinId scl;
        HilPinId sda;
    };

    struct QuadSpiPins
    {
        HilPinId clk;
        HilPinId ncs;
        std::array<HilPinId, 4> io;
    };

    struct LowPowerPins
    {
        HilPinId wake;
        HilPinId marker;
    };
}
