#pragma once

#include "hal/interfaces/AudioOutput.hpp"
#include "hal_st/stm32fxxx/AudioDmaStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/SaiStm.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/WithStorage.hpp"
#include <cstddef>
#include <cstdint>

#if defined(HAS_PERIPHERAL_SAI) && defined(HAL_SAI_MODULE_ENABLED)

namespace hal
{
    class SaiOutputStm
        : public AudioOutput
        , private SaiBlockStm
    {
    public:
        using Config = SaiBlockStm::Config;

        template<std::size_t Samples>
        using WithBuffer = infra::WithStorage<SaiOutputStm, AudioDmaBuffer<Samples>>;

        using SaiBlockStm::ActualSampleRate;
        using SaiBlockStm::IsSupported;

        SaiOutputStm(infra::MemoryRange<int16_t> buffer, SaiStm& sai, DmaStm::TransmitStream& stream, GpioPinStm& sd, GpioPinStm& sck = dummyPinStm, GpioPinStm& fs = dummyPinStm, GpioPinStm& mclk = dummyPinStm, const Config& config = Config());
        ~SaiOutputStm();

        void Start(AudioFormat format, const infra::Function<void(Samples toFill)>& onSamplesRequired, const infra::Function<void()>& onUnderrun) override;
        void Stop() override;
        void SetVolume(uint8_t percent) override;
        void SetMuted(bool muted) override;

    private:
        AudioDmaOutputStm output;
    };
}

#endif
