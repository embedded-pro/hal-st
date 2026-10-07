#include "hal_st/stm32fxxx/Dma2dStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_DMA2D)

namespace hal
{
    namespace
    {
        constexpr uint32_t foregroundLayer = 1;
        constexpr uint32_t backgroundLayer = 0;
        constexpr uint32_t maxLineLength = 0x3fff;
        constexpr uint32_t maxLineOffset = 0x3fff;
        constexpr uint32_t opaqueAlpha = 0xff;
        constexpr uint32_t alphaPosition = 24;

        uint32_t OutputFormat(SurfaceFormat format)
        {
            switch (format)
            {
                case SurfaceFormat::argb8888:
                    return DMA2D_OUTPUT_ARGB8888;
                case SurfaceFormat::rgb888:
                    return DMA2D_OUTPUT_RGB888;
                case SurfaceFormat::rgb565:
                    return DMA2D_OUTPUT_RGB565;
                case SurfaceFormat::argb1555:
                    return DMA2D_OUTPUT_ARGB1555;
                case SurfaceFormat::argb4444:
                    return DMA2D_OUTPUT_ARGB4444;
                case SurfaceFormat::l8:
                case SurfaceFormat::al44:
                case SurfaceFormat::al88:
                case SurfaceFormat::a8:
                case SurfaceFormat::a4:
                    break;
            }

            really_assert(false);
            return 0;
        }

        uint32_t InputFormat(SurfaceFormat format)
        {
            switch (format)
            {
                case SurfaceFormat::argb8888:
                    return DMA2D_INPUT_ARGB8888;
                case SurfaceFormat::rgb888:
                    return DMA2D_INPUT_RGB888;
                case SurfaceFormat::rgb565:
                    return DMA2D_INPUT_RGB565;
                case SurfaceFormat::argb1555:
                    return DMA2D_INPUT_ARGB1555;
                case SurfaceFormat::argb4444:
                    return DMA2D_INPUT_ARGB4444;
                case SurfaceFormat::a8:
                    return DMA2D_INPUT_A8;
                case SurfaceFormat::a4:
                    return DMA2D_INPUT_A4;
                case SurfaceFormat::l8:
                case SurfaceFormat::al44:
                case SurfaceFormat::al88:
                    break;
            }

            really_assert(false);
            return 0;
        }

        template<class Range>
        uint32_t LineOffsetInPixels(const BasicSurface<Range>& surface)
        {
            uint32_t strideInBits = surface.strideInBytes * 8;
            uint32_t bitsPerPixel = BitsPerPixel(surface.format);

            really_assert(strideInBits % bitsPerPixel == 0);

            uint32_t offset = strideInBits / bitsPerPixel - surface.size.width;
            really_assert(offset <= maxLineOffset);
            return offset;
        }

        template<class Range>
        uint32_t Address(Range memory)
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory.begin()));
        }

        template<class Range>
        void CheckTransferSize(const BasicSurface<Range>& surface)
        {
            really_assert(IsValidSurface(surface));
            really_assert(surface.size.width != 0 && surface.size.height != 0 && surface.size.width <= maxLineLength);
        }
    }

    Dma2dStm::Dma2dStm(const Config& config)
        : interruptHandler(peripheralDma2dIrq[0], config.priority, [this]()
              {
                  HAL_DMA2D_IRQHandler(&handle);
              })
    {
        EnableClockDma2d(0);

        handle.Instance = peripheralDma2d[0];
        handle.owner = this;
        handle.Init.Mode = DMA2D_M2M;
        handle.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;
        handle.Init.OutputOffset = 0;

        auto result = HAL_DMA2D_Init(&handle);
        really_assert(result == HAL_OK);

        result = HAL_DMA2D_RegisterCallback(&handle, HAL_DMA2D_TRANSFERCOMPLETE_CB_ID, &Dma2dStm::OnTransferComplete);
        really_assert(result == HAL_OK);

        result = HAL_DMA2D_RegisterCallback(&handle, HAL_DMA2D_TRANSFERERROR_CB_ID, &Dma2dStm::OnTransferError);
        really_assert(result == HAL_OK);
    }

    Dma2dStm::~Dma2dStm()
    {
        if (onDone)
            HAL_DMA2D_Abort(&handle);

        HAL_DMA2D_DeInit(&handle);
        DisableClockDma2d(0);
    }

    bool Dma2dStm::Supports(BlitOperation operation, SurfaceFormat source, SurfaceFormat destination) const
    {
        if (!IsDirectColour(destination))
            return false;

        switch (operation)
        {
            case BlitOperation::fill:
                return true;
            case BlitOperation::copy:
                return IsDirectColour(source);
            case BlitOperation::blend:
                return IsDirectColour(source) || IsAlphaOnly(source);
        }

        return false;
    }

    void Dma2dStm::Fill(const Surface& destination, Argb8888 color, const infra::Function<void()>& onDone)
    {
        CheckTransferSize(destination);
        really_assert(Supports(BlitOperation::fill, destination.format, destination.format));

        Begin(onDone);
        ConfigureOutput(DMA2D_R2M, destination);

        auto result = HAL_DMA2D_Start_IT(&handle, color, Address(destination.memory), destination.size.width, destination.size.height);
        really_assert(result == HAL_OK);
    }

    void Dma2dStm::Copy(const ConstSurface& source, const Surface& destination, const infra::Function<void()>& onDone)
    {
        CheckTransferSize(source);
        CheckTransferSize(destination);
        really_assert(source.size == destination.size);
        really_assert(Supports(BlitOperation::copy, source.format, destination.format));

        Begin(onDone);
        ConfigureOutput(source.format == destination.format ? DMA2D_M2M : DMA2D_M2M_PFC, destination);
        ConfigureCopyForeground(source, destination.format);

        auto result = HAL_DMA2D_Start_IT(&handle, Address(source.memory), Address(destination.memory), destination.size.width, destination.size.height);
        really_assert(result == HAL_OK);
    }

    void Dma2dStm::Blend(const BlendSource& foreground, const ConstSurface& background, const Surface& destination, const infra::Function<void()>& onDone)
    {
        CheckTransferSize(foreground.surface);
        CheckTransferSize(background);
        CheckTransferSize(destination);
        really_assert(foreground.surface.size == destination.size && background.size == destination.size);
        really_assert(Supports(BlitOperation::blend, foreground.surface.format, destination.format) && IsDirectColour(background.format));

        Begin(onDone);
        ConfigureOutput(DMA2D_M2M_BLEND, destination);
        ConfigureBackground(background);
        ConfigureBlendForeground(foreground);

        auto result = HAL_DMA2D_BlendingStart_IT(&handle, Address(foreground.surface.memory), Address(background.memory), Address(destination.memory), destination.size.width, destination.size.height);
        really_assert(result == HAL_OK);
    }

    void Dma2dStm::OnTransferComplete(DMA2D_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->TransferComplete();
    }

    void Dma2dStm::OnTransferError(DMA2D_HandleTypeDef*)
    {
        really_assert(false);
    }

    void Dma2dStm::Begin(const infra::Function<void()>& onDone)
    {
        really_assert(!this->onDone);
        this->onDone = onDone;
    }

    void Dma2dStm::ConfigureOutput(uint32_t mode, const Surface& destination)
    {
        handle.Init.Mode = mode;
        handle.Init.ColorMode = OutputFormat(destination.format);
        handle.Init.OutputOffset = LineOffsetInPixels(destination);

        auto result = HAL_DMA2D_Init(&handle);
        really_assert(result == HAL_OK);
    }

    void Dma2dStm::ConfigureCopyForeground(const ConstSurface& source, SurfaceFormat destinationFormat)
    {
        DMA2D_LayerCfgTypeDef configuration{};
        configuration.InputOffset = LineOffsetInPixels(source);
        configuration.InputColorMode = InputFormat(source.format);

        if (!HasAlpha(source.format) && HasAlpha(destinationFormat))
        {
            configuration.AlphaMode = DMA2D_REPLACE_ALPHA;
            configuration.InputAlpha = opaqueAlpha;
        }
        else
            configuration.AlphaMode = DMA2D_NO_MODIF_ALPHA;

        ConfigureLayer(foregroundLayer, configuration);
    }

    void Dma2dStm::ConfigureBlendForeground(const BlendSource& foreground)
    {
        SurfaceFormat format = foreground.surface.format;

        DMA2D_LayerCfgTypeDef configuration{};
        configuration.InputOffset = LineOffsetInPixels(foreground.surface);
        configuration.InputColorMode = InputFormat(format);

        if (IsAlphaOnly(format))
        {
            configuration.AlphaMode = DMA2D_COMBINE_ALPHA;
            configuration.InputAlpha = uint32_t{ foreground.alpha } << alphaPosition | (foreground.color & 0xffffff);
        }
        else if (!HasAlpha(format))
        {
            configuration.AlphaMode = DMA2D_REPLACE_ALPHA;
            configuration.InputAlpha = foreground.alpha;
        }
        else if (foreground.alpha != 0xff)
        {
            configuration.AlphaMode = DMA2D_COMBINE_ALPHA;
            configuration.InputAlpha = foreground.alpha;
        }
        else
            configuration.AlphaMode = DMA2D_NO_MODIF_ALPHA;

        ConfigureLayer(foregroundLayer, configuration);
    }

    void Dma2dStm::ConfigureBackground(const ConstSurface& background)
    {
        DMA2D_LayerCfgTypeDef configuration{};
        configuration.InputOffset = LineOffsetInPixels(background);
        configuration.InputColorMode = InputFormat(background.format);

        if (HasAlpha(background.format))
            configuration.AlphaMode = DMA2D_NO_MODIF_ALPHA;
        else
        {
            configuration.AlphaMode = DMA2D_REPLACE_ALPHA;
            configuration.InputAlpha = opaqueAlpha;
        }

        ConfigureLayer(backgroundLayer, configuration);
    }

    void Dma2dStm::ConfigureLayer(uint32_t layer, const DMA2D_LayerCfgTypeDef& configuration)
    {
        handle.LayerCfg[layer] = configuration;

        auto result = HAL_DMA2D_ConfigLayer(&handle, layer);
        really_assert(result == HAL_OK);
    }

    void Dma2dStm::TransferComplete()
    {
        onDone();
    }
}

#endif
