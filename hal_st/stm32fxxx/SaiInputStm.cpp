#include "hal_st/stm32fxxx/SaiInputStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_SAI) && defined(HAL_SAI_MODULE_ENABLED)

namespace hal
{
    SaiInputStm::SaiInputStm(infra::MemoryRange<int16_t> buffer, SaiStm& sai, DmaStm::ReceiveStream& stream, GpioPinStm& sd, GpioPinStm& sck, GpioPinStm& fs, GpioPinStm& mclk, const Config& config)
        : SaiBlockStm(sai, Direction::receive, config, sd, sck, fs, mclk)
        , input(stream, DataRegister(), buffer)
    {}

    SaiInputStm::~SaiInputStm()
    {
        StopStream();
    }

    void SaiInputStm::Start(AudioFormat format, const infra::Function<void(Samples)>& onSamples, const infra::Function<void()>& onOverrun)
    {
        really_assert(!input.Armed());

        Configure(format);
        input.Arm(Pdm() ? 2 : format.channels, Pdm() ? 2 : format.channels, onSamples, onOverrun);
        StartPeripheral();
    }

    void SaiInputStm::Stop(const infra::Function<void()>& onStopped)
    {
        StopStream();
        infra::EventDispatcher::Instance().Schedule(onStopped);
    }

    void SaiInputStm::StopStream()
    {
        if (!input.Armed())
            return;

        StopPeripheral();
        input.Disarm();
    }
}

#endif
