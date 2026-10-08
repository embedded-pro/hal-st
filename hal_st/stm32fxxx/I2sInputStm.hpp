#pragma once

#include "hal/interfaces/AudioInput.hpp"
#include "hal_st/stm32fxxx/AudioDmaStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/I2sPeripheralStm.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/WithStorage.hpp"
#include <cstddef>
#include <cstdint>

#if defined(HAS_PERIPHERAL_SPI) && defined(HAL_I2S_MODULE_ENABLED)

namespace hal
{
    class I2sInputStm
        : public AudioInput
        , private I2sPeripheralStm
    {
    public:
        using Config = I2sPeripheralStm::Config;

        template<std::size_t Samples>
        using WithBuffer = infra::WithStorage<I2sInputStm, AudioDmaBuffer<Samples>>;

        using I2sPeripheralStm::ActualSampleRate;
        using I2sPeripheralStm::IsSupported;

        I2sInputStm(infra::MemoryRange<int16_t> buffer, uint8_t oneBasedIndex, DmaStm::ReceiveStream& stream, GpioPinStm& sd, GpioPinStm& ck, GpioPinStm& ws, GpioPinStm& mclk = dummyPinStm, const Config& config = Config());
        ~I2sInputStm();

        void Start(AudioFormat format, const infra::Function<void(Samples)>& onSamples, const infra::Function<void()>& onOverrun) override;
        void Stop() override;

    private:
        AudioDmaInputStm input;
    };
}

#endif
