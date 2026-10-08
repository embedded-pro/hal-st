#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/Blitter.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_DMA2D)

namespace hal
{
    class Dma2dStm
        : public Blitter
    {
    public:
        struct Config
        {
            constexpr Config()
            {}

            cortex::InterruptPriority priority{ cortex::InterruptPriority::normal };
        };

        explicit Dma2dStm(const Config& config = Config());
        ~Dma2dStm();

        bool Supports(BlitOperation operation, SurfaceFormat source, SurfaceFormat destination) const override;
        void Fill(const Surface& destination, Argb8888 color, const infra::Function<void()>& onDone) override;
        void Copy(const ConstSurface& source, const Surface& destination, const infra::Function<void()>& onDone) override;
        void Blend(const BlendSource& foreground, const ConstSurface& background, const Surface& destination, const infra::Function<void()>& onDone) override;

    private:
        struct Handle
            : DMA2D_HandleTypeDef
        {
            Dma2dStm* owner{ nullptr };
        };

        static void OnTransferComplete(DMA2D_HandleTypeDef* handle);
        static void OnTransferError(DMA2D_HandleTypeDef* handle);

        void Begin(const infra::Function<void()>& onDone);
        void ConfigureOutput(uint32_t mode, const Surface& destination);
        void ConfigureCopyForeground(const ConstSurface& source, SurfaceFormat destinationFormat);
        void ConfigureBlendForeground(const BlendSource& foreground);
        void ConfigureBackground(const ConstSurface& background);
        void ConfigureLayer(uint32_t layer, const DMA2D_LayerCfgTypeDef& configuration);
        void TransferComplete();

    private:
        Handle handle{};
        infra::AutoResetFunction<void()> onDone;
        cortex::DispatchedInterruptHandler interruptHandler;
    };
}

#endif
