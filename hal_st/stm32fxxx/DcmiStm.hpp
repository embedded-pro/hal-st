#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/Camera.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/timer/Timer.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_DCMI) && defined(DMA_STREAM_BASED)

namespace hal
{
    class DcmiStm
        : public Camera
    {
    public:
        enum class DataWidth : uint8_t
        {
            bits8,
            bits10,
            bits12,
            bits14
        };

        enum class Edge : uint8_t
        {
            falling,
            rising
        };

        enum class Level : uint8_t
        {
            low,
            high
        };

        struct Window
        {
            uint16_t x;
            uint16_t y;
            uint16_t width;
            uint16_t height;
        };

        struct Config
        {
            constexpr Config()
            {}

            DataWidth dataWidth{ DataWidth::bits8 };
            Edge pixelClockEdge{ Edge::rising };
            Level verticalBlankingLevel{ Level::high };
            Level horizontalBlankingLevel{ Level::low };
            bool jpeg{ false };
            std::optional<Window> crop;
            DmaChannelId dma{ 2, 1, 1 };
            cortex::InterruptPriority priority{ cortex::InterruptPriority::normal };
            std::chrono::milliseconds stopTimeout{ 250 };
        };

        explicit DcmiStm(MultiGpioPinStm& pins, const Config& config = Config());
        ~DcmiStm();
        DcmiStm(const DcmiStm& other) = delete;
        DcmiStm& operator=(const DcmiStm& other) = delete;

        void Start(CameraFormat format, Mode mode, infra::ByteRange buffer, const infra::Function<void(Frame frame)>& onFrame, const infra::Function<void(Error error)>& onError) override;
        void Stop() override;

    private:
        enum class State : uint8_t
        {
            idle,
            active,
            windingDown
        };

        struct Handles
        {
            DCMI_HandleTypeDef dcmi{};
            DMA_HandleTypeDef dma{};
            DcmiStm* owner{ nullptr };
        };

        static void FrameEvent(DCMI_HandleTypeDef* handle);
        static void ErrorEvent(DCMI_HandleTypeDef* handle);

        void EnableClocks() const;
        void InitDcmi();
        void RegisterCallbacks();
        void RegisterInterrupts();
        void InitDma();
        void ConfigureCrop();
        void AssertRequest(const CameraFormat& requestedFormat, Mode requestedMode, infra::ByteRange requestedBuffer) const;
        uint32_t TransferWords() const;

        void Begin();
        void StartCapture();
        void BeginWindDown();
        void PollCaptureStopped();
        void FinishWindDown();
        void ClearCallbacks();

        void OnFrame();
        void OnError();
        void PostEvent(uint8_t event);
        void DeliverEvents();
        void DeliverFrame();
        void DeliverError(Error error);

        void PrepareBufferForCapture() const;
        void InvalidateBuffer() const;

    private:
        MultiPeripheralPinStm pins;
        Config config;
        Handles handles;
        State state{ State::idle };
        bool wanted{ false };
        bool dmaInitialized{ false };
        CameraFormat format{};
        Mode mode{ Mode::snapshot };
        infra::ByteRange buffer;
        infra::Function<void(Frame frame)> onFrame;
        infra::Function<void(Error error)> onError;
        uint32_t remainingStopPolls{ 0 };
        std::atomic<uint8_t> events{ 0 };
        std::atomic<bool> deliveryScheduled{ false };
        std::atomic<uint32_t> capturedBytes{ 0 };
        infra::TimerSingleShot stopTimer;
        std::optional<cortex::ImmediateInterruptHandler> dcmiInterrupt;
        std::optional<cortex::ImmediateInterruptHandler> dmaInterrupt;
    };
}

#endif
