#include "hal_st/stm32fxxx/LtdcStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_LTDC)

namespace hal
{
    namespace
    {
        constexpr std::size_t maxPaletteSize = 256;

        uint32_t LtdcPixelFormat(SurfaceFormat format)
        {
            switch (format)
            {
                case SurfaceFormat::argb8888:
                    return LTDC_PIXEL_FORMAT_ARGB8888;
                case SurfaceFormat::rgb888:
                    return LTDC_PIXEL_FORMAT_RGB888;
                case SurfaceFormat::rgb565:
                    return LTDC_PIXEL_FORMAT_RGB565;
                case SurfaceFormat::argb1555:
                    return LTDC_PIXEL_FORMAT_ARGB1555;
                case SurfaceFormat::argb4444:
                    return LTDC_PIXEL_FORMAT_ARGB4444;
                case SurfaceFormat::l8:
                    return LTDC_PIXEL_FORMAT_L8;
                case SurfaceFormat::al44:
                    return LTDC_PIXEL_FORMAT_AL44;
                case SurfaceFormat::al88:
                    return LTDC_PIXEL_FORMAT_AL88;
                case SurfaceFormat::a8:
                case SurfaceFormat::a4:
                    break;
            }

            really_assert(false);
            return 0;
        }

        bool IsIndexedLtdcPixelFormat(uint32_t pixelFormat)
        {
            return pixelFormat == LTDC_PIXEL_FORMAT_L8 || pixelFormat == LTDC_PIXEL_FORMAT_AL44 || pixelFormat == LTDC_PIXEL_FORMAT_AL88;
        }

        uint32_t Address(infra::ByteRange memory)
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory.begin()));
        }
    }

    LtdcStm::LtdcStm(const DisplayTiming& timing, infra::MemoryRange<const SignalPin> signalPins, const Config& config)
        : timing(timing)
        , config(config)
        , interruptHandler(peripheralLtdcIrq[0], config.priority, [this]()
              {
                  HAL_LTDC_IRQHandler(&handle);
              })
        , errorInterruptHandler(peripheralLtdcErIrq[0], config.priority, [this]()
              {
                  HAL_LTDC_IRQHandler(&handle);
              })
    {
        ConfigurePins(signalPins);
        EnableClockLtdc(0);

        handle.Instance = peripheralLtdc[0];
        handle.owner = this;
        ConfigureTiming();

        auto result = HAL_LTDC_Init(&handle);
        really_assert(result == HAL_OK);

        RegisterCallbacks();

        if (config.dither)
        {
            result = HAL_LTDC_EnableDither(&handle);
            really_assert(result == HAL_OK);
        }
    }

    LtdcStm::~LtdcStm()
    {
        HAL_LTDC_DeInit(&handle);
        DisableClockLtdc(0);
    }

    DisplaySize LtdcStm::Size() const
    {
        return timing.active;
    }

    std::size_t LtdcStm::NumberOfLayers() const
    {
        return numberOfLayers;
    }

    void LtdcStm::Start(const infra::Function<void()>& onVerticalBlank, const infra::Function<void()>& onUnderrun)
    {
        really_assert(!started);

        this->onVerticalBlank = onVerticalBlank;
        this->onUnderrun = onUnderrun;
        started = true;
        ArmLineEvent();
    }

    void LtdcStm::Stop()
    {
        started = false;
        onVerticalBlank = nullptr;
        onUnderrun = nullptr;
        __HAL_LTDC_DISABLE_IT(&handle, LTDC_IT_LI);
    }

    void LtdcStm::ConfigureLayer(std::size_t layer, const DisplayLayer& configuration)
    {
        CheckLayer(layer);

        const Surface& framebuffer = configuration.framebuffer;
        uint32_t bytesPerPixel = BitsPerPixel(framebuffer.format) / 8;

        really_assert(IsValidSurface(framebuffer));
        really_assert(framebuffer.size.width != 0 && framebuffer.size.height != 0);
        really_assert(bytesPerPixel != 0 && framebuffer.strideInBytes % bytesPerPixel == 0);
        really_assert(uint32_t{ configuration.x } + framebuffer.size.width <= timing.active.width);
        really_assert(uint32_t{ configuration.y } + framebuffer.size.height <= timing.active.height);

        bool pixelAlpha = configuration.blendMode == BlendMode::pixelAlpha;

        LTDC_LayerCfgTypeDef layerConfig{};
        layerConfig.WindowX0 = configuration.x;
        layerConfig.WindowX1 = configuration.x + framebuffer.size.width;
        layerConfig.WindowY0 = configuration.y;
        layerConfig.WindowY1 = configuration.y + framebuffer.size.height;
        layerConfig.PixelFormat = LtdcPixelFormat(framebuffer.format);
        layerConfig.Alpha = configuration.alpha;
        layerConfig.Alpha0 = 0;
        layerConfig.BlendingFactor1 = pixelAlpha ? LTDC_BLENDING_FACTOR1_PAxCA : LTDC_BLENDING_FACTOR1_CA;
        layerConfig.BlendingFactor2 = pixelAlpha ? LTDC_BLENDING_FACTOR2_PAxCA : LTDC_BLENDING_FACTOR2_CA;
        layerConfig.FBStartAdress = Address(framebuffer.memory);
        layerConfig.ImageWidth = framebuffer.strideInBytes / bytesPerPixel;
        layerConfig.ImageHeight = framebuffer.size.height;

        auto result = HAL_LTDC_ConfigLayer_NoReload(&handle, &layerConfig, layer);
        really_assert(result == HAL_OK);

        // A read of a shadowed layer register returns the active value, so the read-modify-write in HAL_LTDC_EnableCLUT_NoReload would drop the enable that HAL_LTDC_ConfigLayer_NoReload just set
        WRITE_REG(LTDC_LAYER(&handle, layer)->CR, LTDC_LxCR_LEN | (IsIndexed(framebuffer.format) ? LTDC_LxCR_CLUTEN : 0U));

        framebufferExtents[layer] = (std::size_t{ framebuffer.size.height } - 1) * framebuffer.strideInBytes + std::size_t{ framebuffer.size.width } * bytesPerPixel;
    }

    void LtdcStm::SetFramebuffer(std::size_t layer, infra::ByteRange framebuffer)
    {
        CheckLayer(layer);
        really_assert(framebufferExtents[layer] != 0 && framebuffer.size() >= framebufferExtents[layer]);

        auto result = HAL_LTDC_SetAddress_NoReload(&handle, Address(framebuffer), layer);
        really_assert(result == HAL_OK);
    }

    void LtdcStm::DisableLayer(std::size_t layer)
    {
        CheckLayer(layer);

        __HAL_LTDC_LAYER_DISABLE(&handle, layer);
        framebufferExtents[layer] = 0;
    }

    void LtdcStm::Commit(const infra::Function<void()>& onApplied)
    {
        really_assert(!this->onApplied);

        this->onApplied = onApplied;

        auto result = HAL_LTDC_Reload(&handle, LTDC_RELOAD_VERTICAL_BLANKING);
        really_assert(result == HAL_OK);
    }

    void LtdcStm::SetPalette(std::size_t layer, infra::MemoryRange<const Argb8888> palette)
    {
        CheckLayer(layer);
        really_assert(framebufferExtents[layer] != 0 && IsIndexedLtdcPixelFormat(handle.LayerCfg[layer].PixelFormat));
        really_assert(!palette.empty() && palette.size() <= maxPaletteSize);

        auto result = HAL_LTDC_ConfigCLUT(&handle, const_cast<uint32_t*>(palette.begin()), palette.size(), layer);
        really_assert(result == HAL_OK);
    }

    void LtdcStm::OnLineEvent(LTDC_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->LineEvent();
    }

    void LtdcStm::OnReloadEvent(LTDC_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->ReloadEvent();
    }

    void LtdcStm::OnError(LTDC_HandleTypeDef* handle)
    {
        static_cast<Handle*>(handle)->owner->Error();
    }

    void LtdcStm::ConfigurePins(infra::MemoryRange<const SignalPin> signalPins)
    {
        really_assert(signalPins.size() <= maxSignalPins);

        for (const SignalPin& signalPin : signalPins)
        {
            really_assert(signalPin.signal >= PinConfigTypeStm::ltdcClk && signalPin.signal <= PinConfigTypeStm::ltdcB7);
            pins.emplace_back(signalPin.pin, signalPin.signal, 0);
        }
    }

    void LtdcStm::ConfigureTiming()
    {
        uint32_t horizontalSync = timing.horizontalSync;
        uint32_t verticalSync = timing.verticalSync;
        uint32_t horizontalBackPorch = timing.horizontalBackPorch;
        uint32_t verticalBackPorch = timing.verticalBackPorch;
        uint32_t totalWidth = horizontalSync + horizontalBackPorch + timing.active.width + timing.horizontalFrontPorch;
        uint32_t totalHeight = verticalSync + verticalBackPorch + timing.active.height + timing.verticalFrontPorch;

        really_assert(horizontalSync >= 1 && verticalSync >= 1);
        really_assert(timing.active.width != 0 && timing.active.height != 0);
        really_assert(totalWidth <= 0x1000 && totalHeight <= 0x800);

        handle.Init.HSPolarity = timing.hsyncActiveHigh ? LTDC_HSPOLARITY_AH : LTDC_HSPOLARITY_AL;
        handle.Init.VSPolarity = timing.vsyncActiveHigh ? LTDC_VSPOLARITY_AH : LTDC_VSPOLARITY_AL;
        handle.Init.DEPolarity = timing.dataEnableActiveHigh ? LTDC_DEPOLARITY_AH : LTDC_DEPOLARITY_AL;
        handle.Init.PCPolarity = timing.pixelClockInverted ? LTDC_PCPOLARITY_IIPC : LTDC_PCPOLARITY_IPC;
        handle.Init.HorizontalSync = horizontalSync - 1;
        handle.Init.VerticalSync = verticalSync - 1;
        handle.Init.AccumulatedHBP = horizontalSync + horizontalBackPorch - 1;
        handle.Init.AccumulatedVBP = verticalSync + verticalBackPorch - 1;
        handle.Init.AccumulatedActiveW = horizontalSync + horizontalBackPorch + timing.active.width - 1;
        handle.Init.AccumulatedActiveH = verticalSync + verticalBackPorch + timing.active.height - 1;
        handle.Init.TotalWidth = totalWidth - 1;
        handle.Init.TotalHeigh = totalHeight - 1;
        handle.Init.Backcolor.Blue = config.background & 0xff;
        handle.Init.Backcolor.Green = (config.background >> 8) & 0xff;
        handle.Init.Backcolor.Red = (config.background >> 16) & 0xff;
    }

    void LtdcStm::RegisterCallbacks()
    {
        auto result = HAL_LTDC_RegisterCallback(&handle, HAL_LTDC_LINE_EVENT_CB_ID, &LtdcStm::OnLineEvent);
        really_assert(result == HAL_OK);

        result = HAL_LTDC_RegisterCallback(&handle, HAL_LTDC_RELOAD_EVENT_CB_ID, &LtdcStm::OnReloadEvent);
        really_assert(result == HAL_OK);

        result = HAL_LTDC_RegisterCallback(&handle, HAL_LTDC_ERROR_CB_ID, &LtdcStm::OnError);
        really_assert(result == HAL_OK);
    }

    void LtdcStm::LineEvent()
    {
        if (!started)
            return;

        auto callback = onVerticalBlank;
        ArmLineEvent();
        callback();
    }

    void LtdcStm::ReloadEvent()
    {
        if (onApplied)
            onApplied();
    }

    void LtdcStm::Error()
    {
        really_assert((handle.ErrorCode & HAL_LTDC_ERROR_TE) == 0);

        handle.ErrorCode = HAL_LTDC_ERROR_NONE;
        __HAL_LTDC_ENABLE_IT(&handle, LTDC_IT_FU | LTDC_IT_TE);

        if (started)
        {
            auto callback = onUnderrun;
            callback();
        }
    }

    void LtdcStm::ArmLineEvent()
    {
        auto result = HAL_LTDC_ProgramLineEvent(&handle, handle.Init.AccumulatedActiveH + 1);
        really_assert(result == HAL_OK);
    }

    void LtdcStm::CheckLayer(std::size_t layer) const
    {
        really_assert(layer < numberOfLayers);
    }
}

#endif
