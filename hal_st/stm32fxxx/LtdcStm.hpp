#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/DisplayController.hpp"
#include "hal/interfaces/DisplayTiming.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_LTDC)

namespace hal
{
    class LtdcStm
        : public DisplayController
    {
    public:
        struct SignalPin
        {
            PinConfigTypeStm signal;
            GpioPinStm& pin;
        };

        struct Config
        {
            constexpr Config()
            {}

            Argb8888 background{ 0 };
            bool dither{ false };
            cortex::InterruptPriority priority{ cortex::InterruptPriority::normal };
        };

        static constexpr std::size_t maxSignalPins = 28;
        static constexpr std::size_t numberOfLayers = MAX_LAYER;

        // The pins may be left out when a DSI host takes the pixels instead of a parallel panel
        LtdcStm(const DisplayTiming& timing, infra::MemoryRange<const SignalPin> pins, const Config& config = Config());
        ~LtdcStm();

        DisplaySize Size() const override;
        std::size_t NumberOfLayers() const override;
        void Start(const infra::Function<void()>& onVerticalBlank, const infra::Function<void()>& onUnderrun) override;
        void Stop() override;
        void ConfigureLayer(std::size_t layer, const DisplayLayer& configuration) override;
        void SetFramebuffer(std::size_t layer, infra::ByteRange framebuffer) override;
        void DisableLayer(std::size_t layer) override;
        void Commit(const infra::Function<void()>& onApplied) override;
        void SetPalette(std::size_t layer, infra::MemoryRange<const Argb8888> palette) override;

    private:
        struct Handle
            : LTDC_HandleTypeDef
        {
            LtdcStm* owner{ nullptr };
        };

        static void OnLineEvent(LTDC_HandleTypeDef* handle);
        static void OnReloadEvent(LTDC_HandleTypeDef* handle);
        static void OnError(LTDC_HandleTypeDef* handle);

        void ConfigurePins(infra::MemoryRange<const SignalPin> signalPins);
        void ConfigureTiming();
        void RegisterCallbacks();
        void LineEvent();
        void ReloadEvent();
        void Error();
        void ArmLineEvent();
        void CheckLayer(std::size_t layer) const;

    private:
        DisplayTiming timing;
        Config config;
        infra::BoundedVector<PeripheralPinStm>::WithMaxSize<maxSignalPins> pins;
        Handle handle{};
        bool started{ false };
        infra::Function<void()> onVerticalBlank;
        infra::Function<void()> onUnderrun;
        infra::AutoResetFunction<void()> onApplied;
        std::array<std::size_t, numberOfLayers> framebufferExtents{};
        cortex::DispatchedInterruptHandler interruptHandler;
        cortex::DispatchedInterruptHandler errorInterruptHandler;
    };
}

#endif
