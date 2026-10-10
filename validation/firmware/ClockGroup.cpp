#include "validation/firmware/ClockGroup.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include <cstdint>
#include DEVICE_HEADER
#if defined(STM32G4)
#include "stm32g4xx_ll_rcc.h"
#endif

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;

#if defined(STM32WB)
        constexpr std::array<HilChoice<uint32_t>, 3> rngSources{ {
            { "clk48", RCC_RNGCLKSOURCE_CLK48 },
            { "lsi", RCC_RNGCLKSOURCE_LSI },
            { "lse", RCC_RNGCLKSOURCE_LSE },
        } };

        constexpr std::array<HilChoice<uint32_t>, 4> clk48Sources{ {
            { "hsi48", LL_RCC_CLK48_CLKSOURCE_HSI48 },
            { "pllsai1", LL_RCC_CLK48_CLKSOURCE_PLLSAI1 },
            { "pll", LL_RCC_CLK48_CLKSOURCE_PLL },
            { "msi", LL_RCC_CLK48_CLKSOURCE_MSI },
        } };
#endif
#if defined(STM32G4)
        constexpr std::array<HilChoice<uint32_t>, 2> clk48Sources{ {
            { "hsi48", LL_RCC_RNG_CLKSOURCE_HSI48 },
            { "pll", LL_RCC_RNG_CLKSOURCE_PLL },
        } };
#endif
#if defined(STM32WB) || defined(STM32G4)
        constexpr std::array<HilChoice<uint32_t>, 6> mcoSources{ {
            { "sysclk", LL_RCC_MCO1SOURCE_SYSCLK },
            { "hse", LL_RCC_MCO1SOURCE_HSE },
            { "hsi", LL_RCC_MCO1SOURCE_HSI },
            { "lse", LL_RCC_MCO1SOURCE_LSE },
            { "hsi48", LL_RCC_MCO1SOURCE_HSI48 },
            { "off", LL_RCC_MCO1SOURCE_NOCLOCK },
        } };

        constexpr std::array<HilChoice<uint32_t>, 5> mcoDividers{ {
            { "1", LL_RCC_MCO1_DIV_1 },
            { "2", LL_RCC_MCO1_DIV_2 },
            { "4", LL_RCC_MCO1_DIV_4 },
            { "8", LL_RCC_MCO1_DIV_8 },
            { "16", LL_RCC_MCO1_DIV_16 },
        } };

        constexpr uint32_t hsi48TimeoutUs = 10000;
#else
        constexpr std::array<HilChoice<uint32_t>, 4> rngSources{ {
            { "lse", RCC_RNGCLKSOURCE_LSE },
            { "lsi", RCC_RNGCLKSOURCE_LSI },
            { "hsi", RCC_RNGCLKSOURCE_HSI },
            { "pll", RCC_RNGCLKSOURCE_PLL1Q },
        } };
#endif

        template<std::size_t N>
        const char* NameOf(const std::array<HilChoice<uint32_t>, N>& choices, uint32_t value)
        {
            for (const auto& choice : choices)
                if (choice.value == value)
                    return choice.name;

            return "unknown";
        }
    }

    ClockCommands::ClockCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , commands{ {
              services::HilBind<ClockCommands, &ClockCommands::Info>("clock.info", "", *this, context.response),
#if defined(STM32WB) || defined(STM32G4)
              services::HilBind<ClockCommands, &ClockCommands::Mco>("clock.mco", "<sysclk|hse|hsi|lse|hsi48|off> [div=1|2|4|8|16]", *this, context.response),
              services::HilBind<ClockCommands, &ClockCommands::Hsi48>("clock.hsi48", "<0|1>", *this, context.response),
#endif
          } }
    {}

    infra::MemoryRange<const ClockCommands::Command> ClockCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus ClockCommands::Info(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        auto line = context.response.Ok();
        line << " sysclk=" << HAL_RCC_GetSysClockFreq() << " hclk=" << HAL_RCC_GetHCLKFreq() << " pclk1=" << HAL_RCC_GetPCLK1Freq() << " pclk2=" << HAL_RCC_GetPCLK2Freq();
#if defined(STM32WBA)
        line << " pclk7=" << HAL_RCC_GetPCLK7Freq();
#endif
        line << " hse=" << LL_RCC_HSE_IsReady() << " lse=" << LL_RCC_LSE_IsReady() << " hsi=" << LL_RCC_HSI_IsReady();
#if defined(STM32WB) || defined(STM32G4)
        line << " hsi48=" << LL_RCC_HSI48_IsReady() << " pll=" << LL_RCC_PLL_IsReady();
#else
        line << " pll=" << LL_RCC_PLL1_IsReady();
#endif
#if defined(STM32G4)
        line << " rngsel=clk48";
#else
        line << " rngsel=" << NameOf(rngSources, __HAL_RCC_GET_RNG_SOURCE());
#endif
#if defined(STM32WB) || defined(STM32G4)
#if defined(STM32G4)
        line << " clk48=" << NameOf(clk48Sources, LL_RCC_GetRNGClockSource(LL_RCC_RNG_CLKSOURCE));
#else
        line << " clk48=" << NameOf(clk48Sources, LL_RCC_GetCLK48ClockSource(LL_RCC_CLK48_CLKSOURCE));
#endif
#endif
        return HilStatus::done;
    }

#if defined(STM32WB) || defined(STM32G4)
    HilStatus ClockCommands::Mco(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "div" }))
            return HilStatus::usage;

        uint32_t source = LL_RCC_MCO1SOURCE_NOCLOCK;
        uint32_t divider = LL_RCC_MCO1_DIV_1;
        HilStatus status = HilStatus::done;
        arguments.SelectAt(0, source, mcoSources, status);
        arguments.Select("div", divider, mcoDividers, status);
        if (status != HilStatus::done)
            return status;

        LL_RCC_ConfigMCO(source, divider);
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus ClockCommands::Hsi48(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        uint32_t on = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, on, 0, 1, status);
        if (status != HilStatus::done)
            return status;

        // A running rng driver would lose its kernel clock (CLK48 from HSI48) and abort on the clock error
        if (on == 0 && __HAL_RCC_RNG_IS_CLK_ENABLED() != 0)
            return HilStatus::busy;

        if (on != 0)
            LL_RCC_HSI48_Enable();
        else
            LL_RCC_HSI48_Disable();

        Stopwatch stopwatch;
        stopwatch.Start();
        while (LL_RCC_HSI48_IsReady() != on)
            if (stopwatch.ElapsedUs() > hsi48TimeoutUs)
                return HilStatus::timeout;

        context.response.Ok();
        return HilStatus::done;
    }
#endif
}
