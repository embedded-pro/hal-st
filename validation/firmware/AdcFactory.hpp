#pragma once

#include "hal/interfaces/AdcMultiChannel.hpp"
#include "hal_st/stm32fxxx/AdcDmaMultiChannelStm.hpp"
#include "hal_st/stm32fxxx/AdcTimerTriggeredBase.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/TimerStm.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/hil/commands/HilAdcCommands.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class AdcFactoryStm
        : public services::HilAdcFactory
    {
    public:
        static constexpr std::size_t slots = 1;

        AdcFactoryStm(const services::HilPinNaming& naming, hal::DmaStm& dma, TimerAllocation& timers);

        std::size_t KeyPositionals() const override;
        services::HilStatus ParseKey(const services::HilArguments& arguments, uint16_t& key) const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint16_t key, const services::HilArguments& arguments) override;
        services::HilStatus Open(std::size_t slot, uint16_t key, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilAdcHandle& handle) override;
        void Close(std::size_t slot, uint16_t key, const infra::Function<void()>& onClosed) override;

    private:
        static constexpr std::size_t maximumPins = 8;

        using ChannelConfigs = infra::MemoryRange<const hal::detail::AdcStmChannelConfig>;

        struct Request
        {
            std::array<HilPinId, maximumPins> pins{};
            std::size_t count = 0;
            uint32_t samplingTime = 0;
            std::optional<uint8_t> timer;
            uint32_t rate = 0;
        };

        class Sequence
            : public hal::AdcDmaMultiChannelStmBase
        {
        public:
            template<class Mode>
            Sequence(infra::MemoryRange<uint16_t> samples, infra::MemoryRange<hal::AnalogPinStm> inputs, hal::AdcStm& converter, hal::DmaStm::ReceiveStream& receiveStream, ChannelConfigs configs, Mode mode);

            void Stop() override;
        };

        class TriggeredSequence
            : private hal::AdcTimerTriggeredBase
            , public Sequence
        {
        public:
            TriggeredSequence(infra::MemoryRange<uint16_t> samples, infra::MemoryRange<hal::AnalogPinStm> inputs, hal::AdcStm& converter, hal::DmaStm::ReceiveStream& receiveStream, ChannelConfigs configs, uint8_t oneBasedTimer, hal::TimerBaseStm::Timing timing);

            void Measure(const infra::Function<void(Samples)>& onDone) override;
            void Stop() override;

            bool Measuring() const;

        private:
            std::atomic<bool> measuring{ false };
        };

        class RepeatedConversion
            : public hal::AdcMultiChannel
        {
        public:
            void Attach(Sequence& sequence, Samples samples);
            void Detach();

            void Measure(const infra::Function<void(Samples)>& onDone) override;
            void Stop() override;

        private:
            void Convert();
            void Deliver(uint32_t completed);

        private:
            Sequence* sequence = nullptr;
            Samples samples;
            infra::Function<void(Samples)> onDone;
            bool running = false;
            uint32_t conversion = 0;
        };

        services::HilStatus Parse(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus ParsePins(const services::HilArguments& arguments, Request& request) const;
        void Construct(const Request& request, const std::array<hal::GpioPin*, maximumPins>& claimed, services::HilAdcHandle& handle);

    private:
        const services::HilPinNaming& naming;
        hal::DmaStm& dma;
        TimerAllocation& timers;
        std::optional<hal::AdcStm> adc;
        std::optional<hal::DmaStm::ReceiveStream> stream;
        infra::BoundedVector<hal::AnalogPinStm>::WithMaxSize<maximumPins> analogPins;
        std::array<uint16_t, maximumPins> buffer{};
        std::array<hal::detail::AdcStmChannelConfig, maximumPins> configs{};
        std::variant<std::monostate, Sequence, TriggeredSequence> sequence;
        RepeatedConversion repeated;
        std::optional<uint8_t> timer;
    };
}
