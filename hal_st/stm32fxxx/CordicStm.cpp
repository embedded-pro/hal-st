#include "hal_st/stm32fxxx/CordicStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>

#if defined(HAS_PERIPHERAL_CORDIC)

#if defined(STM32G4)
#include "stm32g4xx_ll_cordic.h"
#endif

namespace hal
{
    namespace
    {
        constexpr std::size_t cordicIndex = 0;

        constexpr std::array<uint32_t, 10> functions{ {
            CORDIC_FUNCTION_COSINE,
            CORDIC_FUNCTION_SINE,
            CORDIC_FUNCTION_PHASE,
            CORDIC_FUNCTION_MODULUS,
            CORDIC_FUNCTION_ARCTANGENT,
            CORDIC_FUNCTION_HCOSINE,
            CORDIC_FUNCTION_HSINE,
            CORDIC_FUNCTION_HARCTANGENT,
            CORDIC_FUNCTION_NATURALLOG,
            CORDIC_FUNCTION_SQUAREROOT,
        } };

        constexpr std::array<uint32_t, CordicBaseStm::maximumPrecision> precisions{ {
            CORDIC_PRECISION_1CYCLE,
            CORDIC_PRECISION_2CYCLES,
            CORDIC_PRECISION_3CYCLES,
            CORDIC_PRECISION_4CYCLES,
            CORDIC_PRECISION_5CYCLES,
            CORDIC_PRECISION_6CYCLES,
            CORDIC_PRECISION_7CYCLES,
            CORDIC_PRECISION_8CYCLES,
            CORDIC_PRECISION_9CYCLES,
            CORDIC_PRECISION_10CYCLES,
            CORDIC_PRECISION_11CYCLES,
            CORDIC_PRECISION_12CYCLES,
            CORDIC_PRECISION_13CYCLES,
            CORDIC_PRECISION_14CYCLES,
            CORDIC_PRECISION_15CYCLES,
        } };

        constexpr std::array<uint32_t, CordicBaseStm::maximumScale + 1> scales{ {
            CORDIC_SCALE_0,
            CORDIC_SCALE_1,
            CORDIC_SCALE_2,
            CORDIC_SCALE_3,
            CORDIC_SCALE_4,
            CORDIC_SCALE_5,
            CORDIC_SCALE_6,
            CORDIC_SCALE_7,
        } };
    }

    CordicBaseStm::CordicBaseStm(const Config& config)
    {
        EnableClockCordic(cordicIndex);
        __HAL_RCC_CORDIC_FORCE_RESET();
        __HAL_RCC_CORDIC_RELEASE_RESET();

        handle.Instance = peripheralCordic[cordicIndex];
        really_assert(HAL_CORDIC_Init(&handle) == HAL_OK);
        Configure(config);
    }

    CordicBaseStm::~CordicBaseStm()
    {
        HAL_CORDIC_DeInit(&handle);
        DisableClockCordic(cordicIndex);
    }

    void CordicBaseStm::Configure(const Config& config)
    {
        really_assert(config.precision >= 1 && config.precision <= maximumPrecision);
        really_assert(config.scale <= maximumScale);
        really_assert(config.arguments == 1 || config.arguments == 2);
        really_assert(config.results == 1 || config.results == 2);

        this->config = config;

        CORDIC_ConfigTypeDef cordicConfig{};
        cordicConfig.Function = functions[static_cast<std::size_t>(config.function)];
        cordicConfig.Scale = scales[config.scale];
        cordicConfig.InSize = config.input == Config::Width::q15 ? CORDIC_INSIZE_16BITS : CORDIC_INSIZE_32BITS;
        cordicConfig.OutSize = config.output == Config::Width::q15 ? CORDIC_OUTSIZE_16BITS : CORDIC_OUTSIZE_32BITS;
        cordicConfig.NbWrite = ArgumentWords() == 2 ? CORDIC_NBWRITE_2 : CORDIC_NBWRITE_1;
        cordicConfig.NbRead = ResultWords() == 2 ? CORDIC_NBREAD_2 : CORDIC_NBREAD_1;
        cordicConfig.Precision = precisions[config.precision - 1];
        really_assert(HAL_CORDIC_Configure(&handle, &cordicConfig) == HAL_OK);
    }

    std::size_t CordicBaseStm::ArgumentWords() const
    {
        return config.input == Config::Width::q15 ? 1 : config.arguments;
    }

    std::size_t CordicBaseStm::ResultWords() const
    {
        return config.output == Config::Width::q15 ? 1 : config.results;
    }

    std::size_t CordicBaseStm::Calculations(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results) const
    {
        auto calculations = arguments.size() / ArgumentWords();
        really_assert(calculations != 0 && arguments.size() == calculations * ArgumentWords());
        really_assert(results.size() == calculations * ResultWords());
        return calculations;
    }

    CORDIC_TypeDef* CordicBaseStm::Instance() const
    {
        return handle.Instance;
    }

    CordicStm::CordicStm(const Config& config)
        : CordicBaseStm(config)
        , interrupt(peripheralCordicIrq[cordicIndex], [this]()
              {
                  ResultReady();
              })
    {}

    CordicStm::~CordicStm()
    {
        LL_CORDIC_DisableIT(Instance());
    }

    void CordicStm::Calculate(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results, const infra::Function<void()>& onDone)
    {
        Calculations(arguments, results);
        really_assert(this->onDone == nullptr);

        this->arguments = arguments;
        this->results = results;
        this->onDone = onDone;

        // The CORDIC requests its interrupt when RRDY becomes set, so a result ready before IEN never raises one
        LL_CORDIC_EnableIT(Instance());
        WriteArguments();
    }

    void CordicStm::ResultReady()
    {
        while (!results.empty() && LL_CORDIC_IsActiveFlag_RRDY(Instance()) != 0)
        {
            for (auto& word : infra::Head(results, ResultWords()))
                word = static_cast<int32_t>(LL_CORDIC_ReadData(Instance()));

            results.pop_front(ResultWords());

            if (!arguments.empty())
                WriteArguments();
        }

        if (results.empty() && onDone != nullptr)
        {
            LL_CORDIC_DisableIT(Instance());
            onDone();
        }
    }

    void CordicStm::WriteArguments()
    {
        for (auto word : infra::Head(arguments, ArgumentWords()))
            LL_CORDIC_WriteData(Instance(), static_cast<uint32_t>(word));

        arguments.pop_front(ArgumentWords());
    }
}

#endif
