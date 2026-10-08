#include "hal_st/stm32fxxx/SaiOutputStm.hpp"
#include "infra/event/EventDispatcher.hpp"
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
        StopStream();
    }

    void SaiOutputStm::Start(AudioFormat format, const infra::Function<void(Samples toFill)>& onSamplesRequired, const infra::Function<void()>& onUnderrun)
    {
        really_assert(!output.Armed());

        Configure(format);
        output.Arm(format.channels, format.channels, onSamplesRequired, onUnderrun);
        StartPeripheral();
    }

    void SaiOutputStm::Stop(const infra::Function<void()>& onStopped)
    {
        StopStream();
        infra::EventDispatcher::Instance().Schedule(onStopped);
    }

    void SaiOutputStm::SetVolume(uint8_t percent, const infra::Function<void()>& onDone)
    {
        output.SetVolume(percent);
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void SaiOutputStm::SetMuted(bool muted, const infra::Function<void()>& onDone)
    {
        output.SetMuted(muted);
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void SaiOutputStm::StopStream()
    {
        if (!output.Armed())
            return;

        StopPeripheral();
        output.Disarm();
    }
}

#endif
