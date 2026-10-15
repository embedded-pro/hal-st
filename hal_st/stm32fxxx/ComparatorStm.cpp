#include "hal_st/stm32fxxx/ComparatorStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_COMP)

#if defined(STM32G4)
#include "stm32g4xx_ll_comp.h"
#include "stm32g4xx_ll_exti.h"
#endif

namespace hal
{
    namespace
    {
        // The DAC selections have one value for every comparator; which DAC channel it reaches depends on the comparator
        constexpr std::array<uint32_t, 8> inputMinusSelections{ {
            COMP_INPUT_MINUS_1_4VREFINT,
            COMP_INPUT_MINUS_1_2VREFINT,
            COMP_INPUT_MINUS_3_4VREFINT,
            COMP_INPUT_MINUS_VREFINT,
            COMP_INPUT_MINUS_DAC3_CH1,
            COMP_INPUT_MINUS_DAC1_CH1,
            COMP_INPUT_MINUS_IO1,
            COMP_INPUT_MINUS_IO2,
        } };

        constexpr std::array<uint32_t, 8> hystereses{ {
            COMP_HYSTERESIS_NONE,
            COMP_HYSTERESIS_10MV,
            COMP_HYSTERESIS_20MV,
            COMP_HYSTERESIS_30MV,
            COMP_HYSTERESIS_40MV,
            COMP_HYSTERESIS_50MV,
            COMP_HYSTERESIS_60MV,
            COMP_HYSTERESIS_70MV,
        } };

        bool InputMinusIsPin(ComparatorBaseStm::Config::InputMinus inputMinus)
        {
            return inputMinus == ComparatorBaseStm::Config::InputMinus::io1 || inputMinus == ComparatorBaseStm::Config::InputMinus::io2;
        }

        bool UpperExtiLines(COMP_TypeDef* instance)
        {
#if defined(COMP7)
            return instance == COMP6 || instance == COMP7;
#else
            return false;
#endif
        }

        bool ExtiFlag(COMP_TypeDef* instance)
        {
            auto line = COMP_GET_EXTI_LINE(instance);
            return UpperExtiLines(instance) ? LL_EXTI_IsActiveFlag_32_63(line) != 0 : LL_EXTI_IsActiveFlag_0_31(line) != 0;
        }

        void ClearExtiFlag(COMP_TypeDef* instance)
        {
            auto line = COMP_GET_EXTI_LINE(instance);
            if (UpperExtiLines(instance))
                LL_EXTI_ClearFlag_32_63(line);
            else
                LL_EXTI_ClearFlag_0_31(line);
        }

        void ConfigureExti(COMP_TypeDef* instance, bool rising, bool falling)
        {
            auto line = COMP_GET_EXTI_LINE(instance);
            if (UpperExtiLines(instance))
            {
                LL_EXTI_DisableIT_32_63(line);
                rising ? LL_EXTI_EnableRisingTrig_32_63(line) : LL_EXTI_DisableRisingTrig_32_63(line);
                falling ? LL_EXTI_EnableFallingTrig_32_63(line) : LL_EXTI_DisableFallingTrig_32_63(line);
                LL_EXTI_ClearFlag_32_63(line);
                if (rising || falling)
                    LL_EXTI_EnableIT_32_63(line);
            }
            else
            {
                LL_EXTI_DisableIT_0_31(line);
                rising ? LL_EXTI_EnableRisingTrig_0_31(line) : LL_EXTI_DisableRisingTrig_0_31(line);
                falling ? LL_EXTI_EnableFallingTrig_0_31(line) : LL_EXTI_DisableFallingTrig_0_31(line);
                LL_EXTI_ClearFlag_0_31(line);
                if (rising || falling)
                    LL_EXTI_EnableIT_0_31(line);
            }
        }
    }

    ComparatorInterruptsStm::ComparatorInterruptsStm()
        : comp123(COMP1_2_3_IRQn, [this]()
              {
                  Invoke(1, 3);
              })
#if defined(COMP4)
#if defined(COMP5)
        , comp456(COMP4_5_6_IRQn, [this]()
              {
                  Invoke(4, 6);
              })
#else
        , comp456(COMP4_IRQn, [this]()
              {
                  Invoke(4, 4);
              })
#endif
#endif
#if defined(COMP7)
        , comp7(COMP7_IRQn, [this]()
              {
                  Invoke(7, 7);
              })
#endif
    {}

    void ComparatorInterruptsStm::Register(uint8_t oneBasedIndex, Type type, const infra::Function<void()>& onEdge)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralComp.size());
        auto& handler = handlers[oneBasedIndex - 1];
        really_assert(handler.onEdge == nullptr);
        handler.type = type;
        handler.onEdge = onEdge;
    }

    void ComparatorInterruptsStm::Unregister(uint8_t oneBasedIndex)
    {
        handlers[oneBasedIndex - 1].onEdge = nullptr;
    }

    void ComparatorInterruptsStm::Invoke(uint8_t first, uint8_t last)
    {
        for (auto index = first; index <= last && index <= peripheralComp.size(); ++index)
        {
            auto instance = peripheralComp[index - 1];
            if (ExtiFlag(instance))
            {
                ClearExtiFlag(instance);
                Edge(index - 1);
            }
        }
    }

    void ComparatorInterruptsStm::Edge(uint8_t index)
    {
        auto& handler = handlers[index];
        if (handler.onEdge == nullptr)
            return;

        if (handler.type == Type::immediate)
            handler.onEdge();
        else if (!handler.scheduled.exchange(true))
            infra::EventDispatcher::Instance().Schedule([this, index]()
                {
                    auto& handler = handlers[index];
                    handler.scheduled = false;
                    if (handler.onEdge != nullptr)
                        handler.onEdge();
                });
    }

    ComparatorBaseStm::ComparatorBaseStm(uint8_t oneBasedIndex, GpioPinStm& inputPlus, GpioPinStm& inputMinus, GpioPinStm& output, const Config& config)
        : index(oneBasedIndex)
        , inputPlus(inputPlus)
        , output(output, PinConfigTypeStm::compOutput, oneBasedIndex)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralComp.size());
        really_assert(config.hysteresisMillivolts <= maximumHysteresisMillivolts && config.hysteresisMillivolts % 10 == 0);

        if (InputMinusIsPin(config.inputMinus))
            this->inputMinus.emplace(inputMinus);

        EnableClockComp(index - 1);
        __HAL_RCC_SYSCFG_CLK_ENABLE();

        handle.Instance = peripheralComp[index - 1];
        handle.Init.InputPlus = config.inputPlus == Config::InputPlus::io2 ? COMP_INPUT_PLUS_IO2 : COMP_INPUT_PLUS_IO1;
        handle.Init.InputMinus = inputMinusSelections[static_cast<std::size_t>(config.inputMinus)];
        handle.Init.Hysteresis = hystereses[config.hysteresisMillivolts / 10];
        handle.Init.OutputPol = config.invertOutput ? COMP_OUTPUTPOL_INVERTED : COMP_OUTPUTPOL_NONINVERTED;
        handle.Init.BlankingSrce = config.blanking;
        handle.Init.TriggerMode = COMP_TRIGGERMODE_NONE;
        really_assert(HAL_COMP_Init(&handle) == HAL_OK);
        really_assert(HAL_COMP_Start(&handle) == HAL_OK);
    }

    ComparatorBaseStm::~ComparatorBaseStm()
    {
        HAL_COMP_Stop(&handle);
        HAL_COMP_DeInit(&handle);
        DisableClockComp(index - 1);
    }

    bool ComparatorBaseStm::Output() const
    {
        // VALUE holds the comparator state before the polarity is applied, unlike the output pin and the EXTI line
        const bool level = LL_COMP_ReadOutputLevel(handle.Instance) == LL_COMP_OUTPUT_LEVEL_HIGH;
        return level != (handle.Init.OutputPol == COMP_OUTPUTPOL_INVERTED);
    }

    uint8_t ComparatorBaseStm::Index() const
    {
        return index;
    }

    COMP_TypeDef* ComparatorBaseStm::Instance() const
    {
        return handle.Instance;
    }

    ComparatorStm::ComparatorStm(uint8_t oneBasedIndex, GpioPinStm& inputPlus, GpioPinStm& inputMinus, GpioPinStm& output, const Config& config)
        : ComparatorBaseStm(oneBasedIndex, inputPlus, inputMinus, output, config)
        , interruptType(config.interruptType)
    {}

    ComparatorStm::~ComparatorStm()
    {
        Disable();
    }

    void ComparatorStm::Enable(const infra::Function<void(bool output)>& onOutputChanged, InterruptTrigger trigger)
    {
        Disable();
        this->onOutputChanged = onOutputChanged;
        ComparatorInterruptsStm::Instance().Register(Index(), interruptType, [this]()
            {
                this->onOutputChanged(GetOutput());
            });
        ConfigureExti(Instance(), trigger != InterruptTrigger::fallingEdge, trigger != InterruptTrigger::risingEdge);
    }

    void ComparatorStm::Disable()
    {
        if (onOutputChanged == nullptr)
            return;

        ConfigureExti(Instance(), false, false);
        ComparatorInterruptsStm::Instance().Unregister(Index());
        onOutputChanged = nullptr;
    }

    bool ComparatorStm::GetOutput() const
    {
        return Output();
    }
}

#endif
