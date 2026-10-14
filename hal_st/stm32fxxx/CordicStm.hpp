#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstddef>
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_CORDIC)

namespace hal
{
    class CordicBaseStm
    {
    public:
        struct Config
        {
            enum class Function : uint8_t
            {
                cosine,
                sine,
                phase,
                modulus,
                arctangent,
                hyperbolicCosine,
                hyperbolicSine,
                hyperbolicArctangent,
                naturalLogarithm,
                squareRoot
            };

            enum class Width : uint8_t
            {
                q31,
                q15
            };

            constexpr Config()
            {}

            Function function{ Function::cosine };
            uint8_t precision{ 6 };
            uint8_t scale{ 0 };
            Width input{ Width::q31 };
            Width output{ Width::q31 };
            uint8_t arguments{ 1 };
            uint8_t results{ 1 };
        };

        static constexpr uint8_t maximumPrecision = 15;
        static constexpr uint8_t maximumScale = 7;

        void Configure(const Config& config);
        std::size_t ArgumentWords() const;
        std::size_t ResultWords() const;

    protected:
        explicit CordicBaseStm(const Config& config);
        CordicBaseStm(const CordicBaseStm& other) = delete;
        CordicBaseStm& operator=(const CordicBaseStm& other) = delete;
        ~CordicBaseStm();

        std::size_t Calculations(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results) const;
        CORDIC_TypeDef* Instance() const;

    protected:
        CORDIC_HandleTypeDef handle{};

    private:
        Config config;
    };

    class CordicStm
        : public CordicBaseStm
    {
    public:
        explicit CordicStm(const Config& config = Config());
        ~CordicStm();

        void Calculate(infra::MemoryRange<const int32_t> arguments, infra::MemoryRange<int32_t> results, const infra::Function<void()>& onDone);

    private:
        void ResultReady();
        void WriteArguments();

    private:
        infra::MemoryRange<const int32_t> arguments;
        infra::MemoryRange<int32_t> results;
        infra::AutoResetFunction<void()> onDone;
        cortex::DispatchedInterruptHandler interrupt;
    };
}

#endif
