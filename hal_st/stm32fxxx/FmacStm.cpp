#include "hal_st/stm32fxxx/FmacStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>

#if defined(HAS_PERIPHERAL_FMAC)

#if defined(STM32G4)
#include "stm32g4xx_ll_fmac.h"
#endif

namespace hal
{
    namespace
    {
        constexpr std::size_t fmacIndex = 0;
        constexpr std::size_t memorySize = 256;
        constexpr std::size_t maximumHeadroom = 4;
        constexpr std::array<int16_t, FmacBaseStm::maximumFirTaps> zeros{};

        int16_t* Preload(infra::MemoryRange<const int16_t> history, std::size_t size)
        {
            really_assert(history.empty() || history.size() == size);
            return const_cast<int16_t*>(history.empty() ? zeros.data() : history.begin());
        }
    }

    FmacBaseStm::FmacBaseStm(const Config& config, uint8_t bufferAccess)
        : config(config)
        , bufferAccess(bufferAccess)
    {
        EnableClockFmac(fmacIndex);
        __HAL_RCC_FMAC_FORCE_RESET();
        __HAL_RCC_FMAC_RELEASE_RESET();

        handle.Instance = peripheralFmac[fmacIndex];
        really_assert(HAL_FMAC_Init(&handle) == HAL_OK);
    }

    FmacBaseStm::~FmacBaseStm()
    {
        Stop();
        HAL_FMAC_DeInit(&handle);
        DisableClockFmac(fmacIndex);
    }

    void FmacBaseStm::ConfigureFir(infra::MemoryRange<const int16_t> b, uint8_t gain, infra::MemoryRange<const int16_t> history)
    {
        really_assert(b.size() >= 2 && b.size() <= maximumFirTaps);
        Configure(FMAC_FUNC_CONVO_FIR, b, {}, gain, history, {});
    }

    void FmacBaseStm::ConfigureIir(infra::MemoryRange<const int16_t> b, infra::MemoryRange<const int16_t> a, uint8_t gain, infra::MemoryRange<const int16_t> history, infra::MemoryRange<const int16_t> outputHistory)
    {
        really_assert(b.size() >= 2 && b.size() <= maximumIirFeedForward);
        really_assert(!a.empty() && a.size() <= maximumIirFeedback);
        Configure(FMAC_FUNC_IIR_DIRECT_FORM_1, b, a, gain, history, outputHistory);
    }

    bool FmacBaseStm::Saturated() const
    {
        return LL_FMAC_IsActiveFlag_SAT(Instance()) != 0;
    }

    FMAC_TypeDef* FmacBaseStm::Instance() const
    {
        return handle.Instance;
    }

    void FmacBaseStm::CheckConfigured() const
    {
        really_assert(started);
    }

    void FmacBaseStm::Configure(uint32_t filter, infra::MemoryRange<const int16_t> b, infra::MemoryRange<const int16_t> a, uint8_t gain, infra::MemoryRange<const int16_t> history, infra::MemoryRange<const int16_t> outputHistory)
    {
        really_assert(gain <= maximumGain);
        Stop();

        auto coefficients = b.size() + a.size();
        auto inputSize = b.size() + 1;
        auto outputSize = a.size() + 1;
        auto headroom = std::min(maximumHeadroom, (memorySize - coefficients - inputSize - outputSize) / 2);

        FMAC_FilterConfigTypeDef filterConfig{};
        filterConfig.CoeffBaseAddress = 0;
        filterConfig.CoeffBufferSize = static_cast<uint8_t>(coefficients);
        filterConfig.InputBaseAddress = static_cast<uint8_t>(coefficients);
        filterConfig.InputBufferSize = static_cast<uint8_t>(inputSize + headroom);
        filterConfig.InputThreshold = FMAC_THRESHOLD_1;
        filterConfig.OutputBaseAddress = static_cast<uint8_t>(coefficients + inputSize + headroom);
        filterConfig.OutputBufferSize = static_cast<uint8_t>(outputSize + headroom);
        filterConfig.OutputThreshold = FMAC_THRESHOLD_1;
        filterConfig.pCoeffB = const_cast<int16_t*>(b.begin());
        filterConfig.CoeffBSize = static_cast<uint8_t>(b.size());
        filterConfig.pCoeffA = a.empty() ? nullptr : const_cast<int16_t*>(a.begin());
        filterConfig.CoeffASize = static_cast<uint8_t>(a.size());
        // HAL_FMAC_FilterStart sets DMAWEN and DMAREN from these before START: the FMAC ignores them once it runs
        filterConfig.InputAccess = bufferAccess;
        filterConfig.OutputAccess = bufferAccess;
        filterConfig.Clip = config.clip ? FMAC_CLIP_ENABLED : FMAC_CLIP_DISABLED;
        filterConfig.Filter = filter;
        filterConfig.P = static_cast<uint8_t>(b.size());
        filterConfig.Q = static_cast<uint8_t>(a.size());
        filterConfig.R = gain;
        really_assert(HAL_FMAC_FilterConfig(&handle, &filterConfig) == HAL_OK);

        auto outputPreload = a.empty() ? nullptr : Preload(outputHistory, a.size());
        really_assert(HAL_FMAC_FilterPreload(&handle, Preload(history, b.size() - 1), static_cast<uint8_t>(b.size() - 1), outputPreload, static_cast<uint8_t>(a.size())) == HAL_OK);
        really_assert(HAL_FMAC_FilterStart(&handle, nullptr, nullptr) == HAL_OK);
        started = true;
    }

    void FmacBaseStm::Stop()
    {
        if (started)
            HAL_FMAC_FilterStop(&handle);

        started = false;
    }

    FmacStm::FmacStm(const Config& config)
        : FmacBaseStm(config)
        , interrupt(peripheralFmacIrq[fmacIndex], [this]()
              {
                  Transfer();
              })
    {}

    FmacStm::~FmacStm()
    {
        LL_FMAC_DisableIT_WR(Instance());
        LL_FMAC_DisableIT_RD(Instance());
    }

    void FmacStm::Filter(infra::MemoryRange<const int16_t> input, infra::MemoryRange<int16_t> output, const infra::Function<void(bool saturated)>& onDone)
    {
        CheckConfigured();
        really_assert(input.size() == output.size() && !input.empty());
        really_assert(this->onDone == nullptr);

        this->input = input;
        this->output = output;
        this->onDone = onDone;

        LL_FMAC_EnableIT_WR(Instance());
        LL_FMAC_EnableIT_RD(Instance());
    }

    void FmacStm::Transfer()
    {
        while (!input.empty() && LL_FMAC_IsActiveFlag_X1FULL(Instance()) == 0)
        {
            LL_FMAC_WriteData(Instance(), static_cast<uint16_t>(input.front()));
            input.pop_front();
        }

        while (!output.empty() && LL_FMAC_IsActiveFlag_YEMPTY(Instance()) == 0)
        {
            output.front() = static_cast<int16_t>(LL_FMAC_ReadData(Instance()));
            output.pop_front();
        }

        if (input.empty())
            LL_FMAC_DisableIT_WR(Instance());

        if (output.empty() && onDone != nullptr)
        {
            LL_FMAC_DisableIT_RD(Instance());
            onDone(Saturated());
        }
    }
}

#endif
