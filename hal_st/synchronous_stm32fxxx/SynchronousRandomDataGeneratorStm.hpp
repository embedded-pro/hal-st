#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/synchronous_interfaces/SynchronousRandomDataGenerator.hpp"

#if defined(HAS_PERIPHERAL_RNG)

namespace hal
{
    class SynchronousRandomDataGeneratorStm
        : public SynchronousRandomDataGenerator
    {
    public:
        SynchronousRandomDataGeneratorStm();
        ~SynchronousRandomDataGeneratorStm();

        void GenerateRandomData(infra::ByteRange result) override;
    };
}

#endif
