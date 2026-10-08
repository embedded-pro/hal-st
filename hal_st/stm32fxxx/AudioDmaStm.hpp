#pragma once

#include "hal_st/stm32fxxx/DataCacheStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hal
{
    template<std::size_t Samples>
    struct alignas(dataCacheLineSize) AudioDmaBuffer
        : std::array<int16_t, Samples>
    {};

    class AudioDmaStm
    {
    protected:
        struct Delivery
        {
            infra::MemoryRange<int16_t> period;
            bool lost;
        };

        AudioDmaStm(infra::MemoryRange<int16_t> buffer, const infra::Function<void()>& deliver);
        AudioDmaStm(const AudioDmaStm& other) = delete;
        AudioDmaStm& operator=(const AudioDmaStm& other) = delete;
        ~AudioDmaStm() = default;

        void Prepare(uint8_t channels, uint8_t slots);
        void Release();
        void PeriodComplete(uint8_t half);
        std::optional<Delivery> TakeDelivery();
        bool MonoOnStereoBus() const;
        bool PeriodElapsed() const;
        uint32_t RunId() const;

    public:
        bool Armed() const;

    protected:
        infra::MemoryRange<int16_t> buffer;

    private:
        infra::Function<void()> deliver;
        std::atomic<uint32_t> periodState{ 0 };
        std::atomic<bool> deliveryScheduled{ false };
        bool armed{ false };
        bool monoOnStereoBus{ false };
        uint32_t runId{ 0 };
    };

    class AudioDmaOutputStm
        : public AudioDmaStm
    {
    public:
        using Samples = infra::MemoryRange<int16_t>;

        AudioDmaOutputStm(DmaStm::TransmitStream& stream, volatile void* dataRegister, infra::MemoryRange<int16_t> buffer);
        ~AudioDmaOutputStm();

        void Arm(uint8_t channels, uint8_t slots, const infra::Function<void(Samples toFill)>& onSamplesRequired, const infra::Function<void()>& onUnderrun);
        void Disarm();

        void SetVolume(uint8_t percent);
        void SetMuted(bool muted);

    private:
        void Deliver();
        void ApplyLevel(Samples period) const;

    private:
        CircularTransmitDmaChannel dma;
        infra::Function<void(Samples toFill)> onSamplesRequired;
        infra::Function<void()> onUnderrun;
        uint32_t gain;
        bool muted{ false };
    };

    class AudioDmaInputStm
        : public AudioDmaStm
    {
    public:
        using Samples = infra::MemoryRange<const int16_t>;

        AudioDmaInputStm(DmaStm::ReceiveStream& stream, volatile void* dataRegister, infra::MemoryRange<int16_t> buffer);
        ~AudioDmaInputStm();

        void Arm(uint8_t channels, uint8_t slots, const infra::Function<void(Samples)>& onSamples, const infra::Function<void()>& onOverrun);
        void Disarm();

    private:
        void Deliver();

    private:
        CircularReceiveDmaChannel dma;
        infra::Function<void(Samples)> onSamples;
        infra::Function<void()> onOverrun;
    };
}
