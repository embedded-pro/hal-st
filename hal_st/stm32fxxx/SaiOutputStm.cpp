#include "hal_st/stm32fxxx/SaiOutputStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_SAI) && defined(HAL_SAI_MODULE_ENABLED)

namespace hal
{
    SaiOutputStm::SaiOutputStm(infra::MemoryRange<int16_t> buffer, SaiStm& sai, DmaStm::TransmitStream& stream, GpioPinStm& sd, GpioPinStm& sck, GpioPinStm& fs, GpioPinStm& mclk, const Config& config)
        : SaiBlockStm(sai, Direction::transmit, config, sd, sck, fs, mclk)
        , output(stream, DataRegister(), buffer)
    {}

    SaiOutputStm::~SaiOutputStm()
    {
        Stop();
    }

    void SaiOutputStm::Start(AudioFormat format, const infra::Function<void(Samples toFill)>& onSamplesRequired, const infra::Function<void()>& onUnderrun)
    {
        really_assert(!output.Armed());

        Configure(format);
        output.Arm(format.channels, format.channels, onSamplesRequired, onUnderrun);
        StartPeripheral();
    }

    void SaiOutputStm::Stop()
    {
        if (!output.Armed())
            return;

        StopPeripheral();
        output.Disarm();
    }

    void SaiOutputStm::SetVolume(uint8_t percent)
    {
        output.SetVolume(percent);
    }

    void SaiOutputStm::SetMuted(bool muted)
    {
        output.SetMuted(muted);
    }
}

#endif
