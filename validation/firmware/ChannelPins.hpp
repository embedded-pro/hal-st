#pragma once

#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace validation
{
    // TimerPwmWithChannels and LpTimerPwmWithChannels index one contiguous MemoryRange<GpioPinStm> by channel, so an
    // unused channel needs a DummyPinStm in its slot rather than a separate object
    template<std::size_t N>
    class ChannelPins
    {
        static_assert(sizeof(hal::DummyPinStm) == sizeof(hal::GpioPinStm) && alignof(hal::DummyPinStm) == alignof(hal::GpioPinStm));

    public:
        ChannelPins() = default;
        ChannelPins(const ChannelPins& other) = delete;
        ChannelPins& operator=(const ChannelPins& other) = delete;
        ~ChannelPins();

        void Emplace(std::size_t position, hal::Port port, uint8_t index);
        void EmplaceUnused(std::size_t position);
        infra::MemoryRange<hal::GpioPinStm> Range(std::size_t count);

    private:
        enum class Kind : uint8_t
        {
            none,
            pin,
            unused,
        };

        void* Slot(std::size_t position);
        void Destroy(std::size_t position);

    private:
        alignas(hal::GpioPinStm) std::array<std::byte, N * sizeof(hal::GpioPinStm)> storage;
        std::array<Kind, N> kinds{};
    };

    ////    Implementation    ////

    template<std::size_t N>
    ChannelPins<N>::~ChannelPins()
    {
        for (std::size_t position = 0; position != N; ++position)
            Destroy(position);
    }

    template<std::size_t N>
    void ChannelPins<N>::Emplace(std::size_t position, hal::Port port, uint8_t index)
    {
        auto slot = Slot(position);
        Destroy(position);
        new (slot) hal::GpioPinStm(port, index);
        kinds[position] = Kind::pin;
    }

    template<std::size_t N>
    void ChannelPins<N>::EmplaceUnused(std::size_t position)
    {
        auto slot = Slot(position);
        Destroy(position);
        new (slot) hal::DummyPinStm();
        kinds[position] = Kind::unused;
    }

    template<std::size_t N>
    infra::MemoryRange<hal::GpioPinStm> ChannelPins<N>::Range(std::size_t count)
    {
        really_assert(count <= N);
        for (std::size_t position = 0; position != count; ++position)
            really_assert(kinds[position] != Kind::none);

        auto first = std::launder(reinterpret_cast<hal::GpioPinStm*>(storage.data()));
        return infra::MemoryRange<hal::GpioPinStm>(first, first + count);
    }

    template<std::size_t N>
    void* ChannelPins<N>::Slot(std::size_t position)
    {
        really_assert(position < N);
        return storage.data() + position * sizeof(hal::GpioPinStm);
    }

    template<std::size_t N>
    void ChannelPins<N>::Destroy(std::size_t position)
    {
        if (kinds[position] == Kind::pin)
            std::destroy_at(std::launder(static_cast<hal::GpioPinStm*>(Slot(position))));
        else if (kinds[position] == Kind::unused)
            std::destroy_at(std::launder(static_cast<hal::DummyPinStm*>(Slot(position))));

        kinds[position] = Kind::none;
    }
}
