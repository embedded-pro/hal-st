#include "hal_st/stm32fxxx/I2sInputStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_SPI) && defined(HAL_I2S_MODULE_ENABLED)

namespace hal
{
    namespace
    {
        constexpr uint8_t slotsPerFrame = 2;
    }

    I2sInputStm::I2sInputStm(infra::MemoryRange<int16_t> buffer, uint8_t oneBasedIndex, DmaStm::ReceiveStream& stream, GpioPinStm& sd, GpioPinStm& ck, GpioPinStm& ws, GpioPinStm& mclk, const Config& config)
        : I2sPeripheralStm(oneBasedIndex, Direction::receive, config, sd, ck, ws, mclk)
        , input(stream, DataRegister(), buffer)
    {}

    I2sInputStm::~I2sInputStm()
    {
        StopStream();
    }

    void I2sInputStm::Start(AudioFormat format, const infra::Function<void(Samples)>& onSamples, const infra::Function<void()>& onOverrun)
    {
        really_assert(!input.Armed());

        Configure(format);
        input.Arm(Pdm() ? slotsPerFrame : format.channels, slotsPerFrame, onSamples, onOverrun);
        StartPeripheral();
    }

    void I2sInputStm::Stop(const infra::Function<void()>& onStopped)
    {
        StopStream();
        infra::EventDispatcher::Instance().Schedule(onStopped);
    }

    void I2sInputStm::StopStream()
    {
        if (!input.Armed())
            return;

        StopPeripheral();
        input.Disarm();
    }
}

#endif
