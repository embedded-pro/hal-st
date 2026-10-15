#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstddef>
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_FMAC)

namespace hal
{
    class FmacBaseStm
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            bool clip{ false };
        };

        static constexpr std::size_t maximumFirTaps = 127;
        static constexpr std::size_t maximumIirFeedForward = 64;
        static constexpr std::size_t maximumIirFeedback = 63;
        static constexpr uint8_t maximumGain = 7;

        // history holds the newest P - 1 input samples, oldest first; an empty history starts from zeros
        void ConfigureFir(infra::MemoryRange<const int16_t> b, uint8_t gain, infra::MemoryRange<const int16_t> history = {});
        // a holds a1..aQ, which the FMAC adds: y[n] = 2^gain * (sum b[k] x[n - k] + sum a[k] y[n - k])
        void ConfigureIir(infra::MemoryRange<const int16_t> b, infra::MemoryRange<const int16_t> a, uint8_t gain, infra::MemoryRange<const int16_t> history = {}, infra::MemoryRange<const int16_t> outputHistory = {});

        bool Saturated() const;

    protected:
        explicit FmacBaseStm(const Config& config, uint8_t bufferAccess = FMAC_BUFFER_ACCESS_POLLING);
        FmacBaseStm(const FmacBaseStm& other) = delete;
        FmacBaseStm& operator=(const FmacBaseStm& other) = delete;
        ~FmacBaseStm();

        FMAC_TypeDef* Instance() const;
        void CheckConfigured() const;

    private:
        void Configure(uint32_t filter, infra::MemoryRange<const int16_t> b, infra::MemoryRange<const int16_t> a, uint8_t gain, infra::MemoryRange<const int16_t> history, infra::MemoryRange<const int16_t> outputHistory);
        void Stop();

    private:
        FMAC_HandleTypeDef handle{};
        Config config;
        uint8_t bufferAccess;
        bool started = false;
    };

    class FmacStm
        : public FmacBaseStm
    {
    public:
        explicit FmacStm(const Config& config = Config());
        ~FmacStm();

        void Filter(infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> output, const infra::Function<void(bool saturated)>& onDone);

    private:
        void Transfer();

    private:
        infra::MemoryRange<const int16_t> input;
        infra::MemoryRange<int16_t> output;
        infra::AutoResetFunction<void(bool saturated)> onDone;
        cortex::DispatchedInterruptHandler interrupt;
    };
}

#endif
