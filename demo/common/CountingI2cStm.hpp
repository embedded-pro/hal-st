#pragma once

#include "hal_st/stm32fxxx/I2cStm.hpp"
#include <cstdint>

namespace main_
{
    class CountingI2cStm
        : public hal::I2cStm
    {
    public:
        using hal::I2cStm::I2cStm;

        uint32_t NotAcknowledged() const
        {
            return notAcknowledged;
        }

        uint32_t BusErrors() const
        {
            return busErrors;
        }

    protected:
        void DeviceNotFound() override
        {
            ++notAcknowledged;
        }

        void BusError() override
        {
            ++busErrors;
        }

        void ArbitrationLost() override
        {
            ++busErrors;
        }

    private:
        uint32_t notAcknowledged = 0;
        uint32_t busErrors = 0;
    };
}
