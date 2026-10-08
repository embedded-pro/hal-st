#include "hal_st/stm32fxxx/I2sOutputStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_SPI) && defined(HAL_I2S_MODULE_ENABLED)

namespace hal
{
    namespace
    {
        constexpr uint8_t slotsPerFrame = 2;
    }

    I2sOutputStm::I2sOutputStm(infra::MemoryRange<int16_t> buffer, uint8_t oneBasedIndex, DmaStm::TransmitStream& stream, GpioPinStm& sd, GpioPinStm& ck, GpioPinStm& ws, GpioPinStm& mclk, const Config& config)
        : I2sPeripheralStm(oneBasedIndex, Direction::transmit, config, sd, ck, ws, mclk)
        , output(stream, DataRegister(), buffer)
    {}

    I2sOutputStm::~I2sOutputStm()
    {
        StopStream();
    }

    void I2sOutputStm::Start(AudioFormat format, const infra::Function<void(Samples toFill)>& onSamplesRequired, const infra::Function<void()>& onUnderrun)
    {
        really_assert(!output.Armed());

        Configure(format);
        output.Arm(format.channels, slotsPerFrame, onSamplesRequired, onUnderrun);
        StartPeripheral();
    }

    void I2sOutputStm::Stop(const infra::Function<void()>& onStopped)
    {
        StopStream();
        infra::EventDispatcher::Instance().Schedule(onStopped);
    }

    void I2sOutputStm::SetVolume(uint8_t percent, const infra::Function<void()>& onDone)
    {
        output.SetVolume(percent);
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void I2sOutputStm::SetMuted(bool muted, const infra::Function<void()>& onDone)
    {
        output.SetMuted(muted);
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void I2sOutputStm::StopStream()
    {
        if (!output.Armed())
            return;

        StopPeripheral();
        output.Disarm();
    }
}

#endif
