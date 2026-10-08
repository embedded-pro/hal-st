#include "hal_st/stm32fxxx/AudioDmaStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace hal
{
    namespace
    {
        constexpr uint32_t maxDmaBytes = 0xFFFF;
        constexpr uint32_t maxPendingPeriods = 2;
        constexpr uint32_t unityGain = 32768;

        uint32_t PackPeriodState(uint32_t pending, uint8_t half)
        {
            return (pending << 1) | half;
        }

        std::size_t Bytes(infra::MemoryRange<int16_t> samples)
        {
            return samples.size() * sizeof(int16_t);
        }

        // The samples occupy the first half of the period and spread to both slots of every frame, back to front so none is overwritten before it is read
        void DuplicateIntoBothSlots(infra::MemoryRange<int16_t> mono)
        {
            for (std::size_t index = mono.size(); index > 0; --index)
            {
                mono.begin()[2 * (index - 1) + 1] = mono.begin()[index - 1];
                mono.begin()[2 * (index - 1)] = mono.begin()[index - 1];
            }
        }

        // Frames arrive as left, right; the left sample moves to the front, front to back so none is overwritten before it is read
        void KeepFirstSlot(infra::MemoryRange<int16_t> stereo)
        {
            for (std::size_t index = 0; index < stereo.size() / 2; ++index)
                stereo.begin()[index] = stereo.begin()[2 * index];
        }
    }

    AudioDmaStm::AudioDmaStm(infra::MemoryRange<int16_t> buffer, const infra::Function<void()>& deliver)
        : buffer(buffer)
        , deliver(deliver)
    {}

    void AudioDmaStm::Prepare(uint8_t channels, uint8_t slots)
    {
        really_assert(!armed);
        really_assert(channels != 0 && slots != 0);
        really_assert(channels == slots || (channels == 1 && slots == 2));
        really_assert(buffer.size() % 2 == 0);
        really_assert((buffer.size() / 2) % slots == 0);
        really_assert(Bytes(buffer) <= maxDmaBytes);

        if (DataCacheEnabled())
            really_assert(reinterpret_cast<uintptr_t>(buffer.begin()) % dataCacheLineSize == 0 && (Bytes(buffer) / 2) % dataCacheLineSize == 0);

        periodState = 0;
        monoOnStereoBus = channels != slots;
        ++runId;
        armed = true;
    }

    void AudioDmaStm::Release()
    {
        armed = false;
        periodState = 0;
    }

    bool AudioDmaStm::Armed() const
    {
        return armed;
    }

    bool AudioDmaStm::MonoOnStereoBus() const
    {
        return monoOnStereoBus;
    }

    // The other half having completed means the DMA is already back in the half that was just delivered
    bool AudioDmaStm::PeriodElapsed() const
    {
        return periodState.load(std::memory_order_acquire) != 0;
    }

    uint32_t AudioDmaStm::RunId() const
    {
        return runId;
    }

    void AudioDmaStm::PeriodComplete(uint8_t half)
    {
        // Runs in the DMA interrupt, which cannot be preempted by TakeDelivery, so a plain read-modify-write is safe
        const uint32_t pending = std::min<uint32_t>((periodState.load(std::memory_order_relaxed) >> 1) + 1, maxPendingPeriods);
        periodState.store(PackPeriodState(pending, half), std::memory_order_release);

        if (!deliveryScheduled.exchange(true))
            infra::EventDispatcher::Instance().Schedule(deliver);
    }

    std::optional<AudioDmaStm::Delivery> AudioDmaStm::TakeDelivery()
    {
        deliveryScheduled = false;
        const uint32_t state = periodState.exchange(0);

        if (!armed || state == 0)
            return std::nullopt;

        const std::size_t periodSize = buffer.size() / 2;
        const std::size_t offset = (state & 1) * periodSize;

        return Delivery{ infra::MemoryRange<int16_t>(buffer.begin() + offset, buffer.begin() + offset + periodSize), (state >> 1) > 1 };
    }

    AudioDmaOutputStm::AudioDmaOutputStm(DmaStm::TransmitStream& stream, volatile void* dataRegister, infra::MemoryRange<int16_t> buffer)
        : AudioDmaStm(buffer, [this]
              {
                  Deliver();
              })
        , dma(stream, dataRegister, sizeof(int16_t), [this]
              {
                  PeriodComplete(0);
              },
              [this]
              {
                  PeriodComplete(1);
              })
        , gain(unityGain)
    {}

    AudioDmaOutputStm::~AudioDmaOutputStm()
    {
        if (Armed())
            Disarm();
    }

    void AudioDmaOutputStm::Arm(uint8_t channels, uint8_t slots, const infra::Function<void(Samples toFill)>& onSamplesRequired, const infra::Function<void()>& onUnderrun)
    {
        Prepare(channels, slots);

        this->onSamplesRequired = onSamplesRequired;
        this->onUnderrun = onUnderrun;

        std::fill(buffer.begin(), buffer.end(), 0);
        CleanDataCache(buffer.begin(), Bytes(buffer));

        dma.StartTransmit(infra::MemoryRange<const int16_t>(buffer));
    }

    void AudioDmaOutputStm::Disarm()
    {
        Release();
        dma.StopTransfer();

        onSamplesRequired = nullptr;
        onUnderrun = nullptr;
    }

    void AudioDmaOutputStm::SetVolume(uint8_t percent)
    {
        really_assert(percent <= 100);

        gain = percent * unityGain / 100;
    }

    void AudioDmaOutputStm::SetMuted(bool muted)
    {
        this->muted = muted;
    }

    void AudioDmaOutputStm::Deliver()
    {
        auto delivery = TakeDelivery();

        if (!delivery)
            return;

        const uint32_t run = RunId();
        std::fill(delivery->period.begin(), delivery->period.end(), 0);

        const Samples toFill = MonoOnStereoBus() ? Samples(delivery->period.begin(), delivery->period.begin() + delivery->period.size() / 2) : delivery->period;

        auto samplesRequired = onSamplesRequired;
        samplesRequired(toFill);

        if (!Armed() || RunId() != run)
            return;

        ApplyLevel(toFill);

        if (MonoOnStereoBus())
            DuplicateIntoBothSlots(toFill);

        CleanDataCache(delivery->period.begin(), Bytes(delivery->period));

        if (delivery->lost || PeriodElapsed())
        {
            auto underrun = onUnderrun;
            underrun();
        }
    }

    void AudioDmaOutputStm::ApplyLevel(Samples period) const
    {
        if (muted)
            std::fill(period.begin(), period.end(), 0);
        else if (gain != unityGain)
            std::transform(period.begin(), period.end(), period.begin(), [this](int16_t sample)
                {
                    return static_cast<int16_t>((static_cast<int32_t>(sample) * static_cast<int32_t>(gain)) >> 15);
                });
    }

    AudioDmaInputStm::AudioDmaInputStm(DmaStm::ReceiveStream& stream, volatile void* dataRegister, infra::MemoryRange<int16_t> buffer)
        : AudioDmaStm(buffer, [this]
              {
                  Deliver();
              })
        , dma(stream, dataRegister, sizeof(int16_t), [this]
              {
                  PeriodComplete(0);
              },
              [this]
              {
                  PeriodComplete(1);
              })
    {}

    AudioDmaInputStm::~AudioDmaInputStm()
    {
        if (Armed())
            Disarm();
    }

    void AudioDmaInputStm::Arm(uint8_t channels, uint8_t slots, const infra::Function<void(Samples)>& onSamples, const infra::Function<void()>& onOverrun)
    {
        Prepare(channels, slots);

        this->onSamples = onSamples;
        this->onOverrun = onOverrun;

        InvalidateDataCache(buffer.begin(), Bytes(buffer));

        dma.StartReceive(buffer);
    }

    void AudioDmaInputStm::Disarm()
    {
        Release();
        dma.StopTransfer();

        onSamples = nullptr;
        onOverrun = nullptr;
    }

    void AudioDmaInputStm::Deliver()
    {
        auto delivery = TakeDelivery();

        if (!delivery)
            return;

        const uint32_t run = RunId();
        const bool mono = MonoOnStereoBus();

        InvalidateDataCache(delivery->period.begin(), Bytes(delivery->period));

        if (mono)
            KeepFirstSlot(delivery->period);

        const Samples received = mono ? Samples(delivery->period.begin(), delivery->period.begin() + delivery->period.size() / 2) : Samples(delivery->period);

        auto samples = onSamples;
        samples(received);

        // The converted samples would be written back over what the DMA stores here next, so they are dropped from the cache
        if (mono)
            InvalidateDataCache(delivery->period.begin(), Bytes(delivery->period));

        if (!Armed() || RunId() != run)
            return;

        if (delivery->lost || PeriodElapsed())
        {
            auto overrun = onOverrun;
            overrun();
        }
    }
}
