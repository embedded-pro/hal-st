#ifndef EXAMPLES_DISPLAY_DEMO_DISPLAY_DEMO_HPP
#define EXAMPLES_DISPLAY_DEMO_DISPLAY_DEMO_HPP

#include "hal/interfaces/Blitter.hpp"
#include "hal/interfaces/DisplayController.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace examples
{
    // Draws into a window with a blitter and shows it through a display controller, using two frame buffers.
    // A moving bar, a sprite whose format is converted while it is copied and an alpha mask are blended
    // into the back buffer, while a translucent overlay layer stays above the window.
    // Only the interfaces of the hardware abstraction layer are used, so every board runs the same demo
    class DisplayDemo
    {
    public:
        struct Config
        {
            hal::DisplayArea window;
            hal::SurfaceFormat windowFormat;
            hal::DisplayArea overlay;
        };

        static std::size_t RequiredMemory(const Config& config);

        DisplayDemo(hal::DisplayController& display, hal::Blitter& blitter, infra::ByteRange memory, const Config& config);

        void Start();

        std::size_t FramesShown() const;
        std::size_t VerticalBlanks() const;
        std::size_t Underruns() const;

    private:
        enum class Step : uint8_t
        {
            prepareFront,
            prepareBack,
            prepareOverlay,
            showInitial,
            clearBack,
            drawBar,
            copySprite,
            blendMask,
            present
        };

        void RunStep();
        void NextStep();
        void ShowInitialLayers();
        void Present();
        void FrameShown();
        infra::Function<void()> StepDone();
        hal::Surface Window(std::size_t buffer) const;
        hal::DisplayArea BarArea() const;
        hal::DisplayArea SpriteArea() const;
        hal::DisplayArea MaskArea() const;

    private:
        hal::DisplayController& display;
        hal::Blitter& blitter;
        Config config;
        std::array<hal::Surface, 2> buffers;
        hal::Surface overlay;
        std::array<uint32_t, 16 * 16> sprite;
        std::array<uint8_t, 32 * 32> mask;
        std::size_t front{ 0 };
        std::size_t frame{ 0 };
        std::size_t framesShown{ 0 };
        std::size_t verticalBlanks{ 0 };
        std::size_t underruns{ 0 };
        Step step{ Step::prepareFront };
    };
}

#endif
