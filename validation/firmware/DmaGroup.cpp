#include "validation/firmware/DmaGroup.hpp"
#include "BoardProfile.hpp"
#include "validation/firmware/AdcFactory.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <chrono>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using services::HilStatus;

        // board::dmaGroupDma serves the TIM2 update request
        constexpr uint8_t waveTimer = 2;
        constexpr uint32_t maximumRate = 1000000;
        constexpr uint32_t defaultDurationMs = 50;
        constexpr uint32_t maximumDurationMs = 10000;

        constexpr std::array<const char*, 3> waveKeys{ { "rate", "pattern", "ms" } };

        constexpr Resource dmaResource = board::dmaGroupDma.dma == 1 ? Resource::dma1 : Resource::dma2;

        GPIO_TypeDef* PortRegisters(hal::Port port)
        {
            const std::array ports{
#if defined(GPIOA)
                GPIOA,
#endif
#if defined(GPIOB)
                GPIOB,
#endif
#if defined(GPIOC)
                GPIOC,
#endif
#if defined(GPIOD)
                GPIOD,
#endif
#if defined(GPIOE)
                GPIOE,
#endif
#if defined(GPIOF)
                GPIOF,
#endif
#if defined(GPIOG)
                GPIOG,
#endif
#if defined(GPIOH)
                GPIOH,
#endif
            };

            return ports[static_cast<std::size_t>(port)];
        }
    }

    DmaCommands::DmaCommands(services::HilContext& context, const services::HilPinNaming& naming, hal::DmaStm& dma, TimerAllocation& timers, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , naming(naming)
        , dma(dma)
        , timers(timers)
        , resources(resources)
        , pins(context.pins, owners::dma)
        , commands{ {
              services::HilBind<DmaCommands, &DmaCommands::Wave>("dma.wave", "<pin> rate= pattern= [ms=]", *this, context.response),
          } }
    {}

    infra::MemoryRange<const DmaCommands::Command> DmaCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus DmaCommands::Wave(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(waveKeys)) || !arguments.Has("rate") || !arguments.Has("pattern"))
            return HilStatus::usage;

        uint32_t rate = 0;
        uint32_t durationMs = defaultDurationMs;
        std::array<uint8_t, maximumPatternBytes> pattern{};
        std::size_t patternSize = 0;
        HilPinId pin;
        HilStatus status = HilStatus::done;
        arguments.Number("rate", rate, 1, maximumRate, status);
        if (status == HilStatus::done)
            status = services::HilArguments::ParseHex(*arguments.Key("pattern"), infra::MakeRange(pattern), patternSize);
        if (status == HilStatus::done && patternSize == 0)
            status = HilStatus::usage;
        arguments.Number("ms", durationMs, 1, maximumDurationMs, status);
        arguments.PinAt(0, naming, pin, status);
        if (status != HilStatus::done)
            return status;

        if (!IsBonded(pin))
            return HilStatus::pin;

        if (channel)
            return HilStatus::busy;

        status = Claim(pin);
        if (status != HilStatus::done)
            return status;

        claimedPin->Config(hal::PinConfigType::output, false);
        const auto count = FillWords(pin, infra::Head(infra::MakeRange(pattern), patternSize));

        auto& updates = timer.emplace(waveTimer, TriggerTiming(waveTimer, rate), hal::TimerBaseStm::Config{ hal::TimerBaseStm::CounterMode::up });
        auto& transmitStream = stream.emplace(dma, hal::DmaChannelId(board::dmaGroupDma.dma, board::dmaGroupDma.channel, board::dmaGroupDmaRequest));
        auto& circular = channel.emplace(transmitStream, &PortRegisters(PortOf(pin))->BSRR, sizeof(uint32_t), []() {}, []() {});
        circular.StartTransmit(infra::MemoryRange<const uint32_t>(words.data(), words.data() + count));
        __HAL_TIM_ENABLE_DMA(&updates.Handle(), TIM_DMA_UPDATE);
        updates.Start();

        duration.Start(std::chrono::milliseconds(durationMs), [this]()
            {
                Finish();
            });

        return HilStatus::done;
    }

    HilStatus DmaCommands::Claim(HilPinId pin)
    {
        HilStatus status = pins.Claim(pin, services::HilPinPool::Use::exclusive, claimedPin);
        if (status == HilStatus::done)
            status = timers.Claim(waveTimer, owners::dma);
        if (status == HilStatus::done)
            status = resources.Claim(dmaResource, board::dmaGroupDma.channel, owners::dma);

        if (status != HilStatus::done)
            Release();

        return status;
    }

    void DmaCommands::Release()
    {
        pins.Release();
        claimedPin = nullptr;
        timers.Release(waveTimer, owners::dma);
        resources.Release(dmaResource, board::dmaGroupDma.channel, owners::dma);
    }

    // The circular channel always enables its half and full transfer interrupts: repeating the pattern over the whole buffer keeps them rare at high rates
    std::size_t DmaCommands::FillWords(HilPinId pin, infra::ConstByteRange pattern)
    {
        const auto bits = pattern.size() * 8;
        const auto count = bits * (words.size() / bits);
        const uint32_t set = 1u << pin.index;
        const uint32_t reset = set << 16;

        for (std::size_t i = 0; i != count; ++i)
        {
            const auto bit = i % bits;
            words[i] = (pattern[bit / 8] & (1u << (bit % 8))) != 0 ? set : reset;
        }

        return count;
    }

    void DmaCommands::Finish()
    {
        timer->Stop();
        channel->StopTransfer();
        __HAL_TIM_DISABLE_DMA(&timer->Handle(), TIM_DMA_UPDATE);

        channel.reset();
        stream.reset();
        timer.reset();
        Release();

        context.response.Ok();
    }
}
