#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/AnalogComparator.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_COMP)

namespace hal
{
    class ComparatorInterruptsStm
        : public infra::InterfaceConnector<ComparatorInterruptsStm>
    {
    public:
        enum class Type : uint8_t
        {
            immediate,
            dispatched
        };

        ComparatorInterruptsStm();

        void Register(uint8_t oneBasedIndex, Type type, const infra::Function<void()>& onEdge);
        void Unregister(uint8_t oneBasedIndex);

    private:
        struct Handler
        {
            infra::Function<void()> onEdge;
            Type type{ Type::dispatched };
            std::atomic<bool> scheduled{ false };
        };

        void Invoke(uint8_t first, uint8_t last);
        void Edge(uint8_t index);

    private:
        std::array<Handler, 7> handlers;
        // One vector serves the lines of several comparators, each with an immediate or dispatched callback
        cortex::ImmediateInterruptHandler comp123;
#if defined(COMP4)
        cortex::ImmediateInterruptHandler comp456;
#endif
#if defined(COMP7)
        cortex::ImmediateInterruptHandler comp7;
#endif
    };

    class ComparatorBaseStm
    {
    public:
        struct Config
        {
            enum class InputPlus : uint8_t
            {
                io1,
                io2
            };

            enum class InputMinus : uint8_t
            {
                vrefintQuarter,
                vrefintHalf,
                vrefintThreeQuarters,
                vrefint,
                dacInternal,
                dacExternal,
                io1,
                io2
            };

            constexpr Config()
            {}

            InputPlus inputPlus{ InputPlus::io1 };
            InputMinus inputMinus{ InputMinus::vrefintHalf };
            uint8_t hysteresisMillivolts{ 0 };
            bool invertOutput{ false };
            uint32_t blanking{ COMP_BLANKINGSRC_NONE };
            ComparatorInterruptsStm::Type interruptType{ ComparatorInterruptsStm::Type::dispatched };
        };

        static constexpr uint8_t maximumHysteresisMillivolts = 70;

        bool Output() const;

    protected:
        ComparatorBaseStm(uint8_t oneBasedIndex, GpioPinStm& inputPlus, GpioPinStm& inputMinus, GpioPinStm& output, const Config& config);
        ComparatorBaseStm(const ComparatorBaseStm& other) = delete;
        ComparatorBaseStm& operator=(const ComparatorBaseStm& other) = delete;
        ~ComparatorBaseStm();

        uint8_t Index() const;
        COMP_TypeDef* Instance() const;

    private:
        uint8_t index;
        AnalogPinStm inputPlus;
        std::optional<AnalogPinStm> inputMinus;
        PeripheralPinStm output;
        COMP_HandleTypeDef handle{};
    };

    class ComparatorStm
        : public ComparatorBaseStm
        , public AnalogComparator
    {
    public:
        ComparatorStm(uint8_t oneBasedIndex, GpioPinStm& inputPlus, GpioPinStm& inputMinus, GpioPinStm& output = dummyPinStm, const Config& config = Config());
        ~ComparatorStm();

        void Enable(const infra::Function<void(bool output)>& onOutputChanged, InterruptTrigger trigger) override;
        void Disable() override;
        bool GetOutput() const override;

    private:
        ComparatorInterruptsStm::Type interruptType;
        infra::Function<void(bool output)> onOutputChanged;
    };
}

#endif
