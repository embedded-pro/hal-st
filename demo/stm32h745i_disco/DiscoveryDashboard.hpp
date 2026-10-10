#pragma once

#include "boards/rk043fn48h/Rk043fn48h.hpp"
#include "demo/common/Canvas.hpp"
#include "demo/common/DashboardStyle.hpp"
#include "demo/common/TouchCursor.hpp"
#include "hal/interfaces/Blitter.hpp"
#include "hal/interfaces/DisplayController.hpp"
#include "hal/interfaces/TouchScreen.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace main_
{
    class DiscoveryDashboard
    {
    public:
        static constexpr hal::DisplaySize screenSize = boards::rk043fn48hTiming.active;
        static constexpr std::size_t frameBytes = screenSize.width * screenSize.height * 2;

        enum class Item : uint8_t
        {
            display,
            sdram,
            qspi,
            audio,
            touch,
            count
        };

        using State = StatusState;

        DiscoveryDashboard(hal::DisplayController& display, hal::Blitter& blitter, infra::ByteRange frameMemory, infra::ByteRange cursorMemory);

        void Start(const infra::Function<void()>& onStarted = infra::emptyFunction);

        void SetStatus(Item item, State state, infra::BoundedConstString detail);
        void SetButton(bool pressed);
        void SetUptime(uint32_t seconds);
        void SetTouch(hal::TouchScreen::Phase phase, hal::TouchPoint point);
        void ClearTouchPad();

        std::size_t FramesShown() const;
        std::size_t Underruns() const;

    private:
        static constexpr std::size_t itemCount = static_cast<std::size_t>(Item::count);

        void DrawAll();
        void DrawTitle();
        void DrawStatus(Item item);
        void DrawButton();
        void DrawTouchPad();
        void DrawTouchText();
        void PaintTouchDot(hal::TouchPoint point);
        void ShowLayers();

    private:
        hal::DisplayController& display;
        hal::Blitter& blitter;
        hal::Surface frame;
        Canvas canvas;
        TouchCursor cursor;
        std::array<State, itemCount> states{};
        std::array<infra::BoundedString::WithStorage<28>, itemCount> details;
        bool buttonPressed{ false };
        uint32_t uptime{ 0 };
        hal::TouchScreen::Phase touchPhase{ hal::TouchScreen::Phase::released };
        hal::TouchPoint touchPoint{ 0, 0 };
        infra::Function<void()> onStarted;
        bool drawn{ false };
        std::size_t framesShown{ 0 };
        std::size_t underruns{ 0 };
    };
}
