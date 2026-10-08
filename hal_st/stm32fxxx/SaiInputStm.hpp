#pragma once

#include "hal/interfaces/AudioInput.hpp"
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
    class SaiInputStm
        : public AudioInput
        , private SaiBlockStm
    {
    public:
        using Config = SaiBlockStm::Config;

        template<std::size_t Samples>
        using WithBuffer = infra::WithStorage<SaiInputStm, AudioDmaBuffer<Samples>>;

        using SaiBlockStm::ActualSampleRate;
        using SaiBlockStm::IsSupported;

        SaiInputStm(infra::MemoryRange<int16_t> buffer, SaiStm& sai, DmaStm::ReceiveStream& stream, GpioPinStm& sd, GpioPinStm& sck = dummyPinStm, GpioPinStm& fs = dummyPinStm, GpioPinStm& mclk = dummyPinStm, const Config& config = Config());
        ~SaiInputStm();

        void Start(AudioFormat format, const infra::Function<void(Samples)>& onSamples, const infra::Function<void()>& onOverrun) override;
        void Stop(const infra::Function<void()>& onStopped) override;

    private:
        void StopStream();

    private:
        AudioDmaInputStm input;
    };
}

#endif
