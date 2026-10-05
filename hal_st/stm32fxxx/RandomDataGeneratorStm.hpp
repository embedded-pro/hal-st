#ifndef HAL_RANDOM_DATA_GENERATOR_STM_HPP
#define HAL_RANDOM_DATA_GENERATOR_STM_HPP

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/RandomDataGenerator.hpp"

#if defined(HAS_PERIPHERAL_RNG)

namespace hal
{
    class RandomDataGeneratorStm
        : public RandomDataGenerator
        , private cortex::InterruptHandler
    {
    public:
        RandomDataGeneratorStm();
        ~RandomDataGeneratorStm();

        void GenerateRandomData(infra::ByteRange result, const infra::Function<void()>& onDone) override;

    private:
        void Invoke() override;

    private:
        infra::ByteRange result;
        infra::Function<void()> onDone;
    };
}

#endif

#endif
