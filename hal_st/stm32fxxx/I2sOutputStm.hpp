#pragma once

#include "hal/interfaces/AudioOutput.hpp"
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
    class I2sOutputStm
        : public AudioOutput
        , private I2sPeripheralStm
    {
    public:
        using Config = I2sPeripheralStm::Config;

        template<std::size_t Samples>
        using WithBuffer = infra::WithStorage<I2sOutputStm, AudioDmaBuffer<Samples>>;

        using I2sPeripheralStm::ActualSampleRate;
        using I2sPeripheralStm::IsSupported;

        I2sOutputStm(infra::MemoryRange<int16_t> buffer, uint8_t oneBasedIndex, DmaStm::TransmitStream& stream, GpioPinStm& sd, GpioPinStm& ck, GpioPinStm& ws, GpioPinStm& mclk = dummyPinStm, const Config& config = Config());
        ~I2sOutputStm();

        void Start(AudioFormat format, const infra::Function<void(Samples toFill)>& onSamplesRequired, const infra::Function<void()>& onUnderrun) override;
        void Stop(const infra::Function<void()>& onStopped) override;
        void SetVolume(uint8_t percent, const infra::Function<void()>& onDone) override;
        void SetMuted(bool muted, const infra::Function<void()>& onDone) override;

    private:
        void StopStream();

    private:
        AudioDmaOutputStm output;
    };
}

#endif
