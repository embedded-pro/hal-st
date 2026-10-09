#pragma once

#include "demo/stm32h757i_eval/Canvas.hpp"
#include "hal/interfaces/Blitter.hpp"
#include "hal/interfaces/DisplayController.hpp"
#include "hal/interfaces/TouchScreen.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace main_
{
    class Dashboard
    {
    public:
        static constexpr hal::DisplaySize screenSize{ 800, 480 };
        static constexpr std::size_t frameBytes = screenSize.width * screenSize.height * 2;
        static constexpr uint16_t cursorSide = 28;
        static constexpr std::size_t cursorBytes = cursorSide * cursorSide * 4;

        enum class Item : uint8_t
        {
            display,
            sdram,
            sram,
            nor,
            qspi,
            audio,
            touch,
            mfx,
            adc,
            dac,
            count
        };

        enum class State : uint8_t
        {
            pending,
            ok,
            failed
        };

        enum class Button : uint8_t
        {
            wakeup,
            tamper,
            select,
            up,
            down,
            left,
            right,
            count
        };

        Dashboard(hal::DisplayController& display, hal::Blitter& blitter, infra::ByteRange frameMemory, infra::ByteRange cursorMemory);

        void Start();

        void SetStatus(Item item, State state, infra::BoundedConstString detail);
        void SetButton(Button button, bool pressed);
        void SetPotentiometer(uint16_t counts);
        void SetDac(uint16_t counts);
        void SetUptime(uint32_t seconds);
        void SetTouch(hal::TouchScreen::Phase phase, hal::TouchPoint point);
        void ClearTouchPad();

        std::size_t FramesShown() const;
        std::size_t Underruns() const;

    private:
        static constexpr std::size_t itemCount = static_cast<std::size_t>(Item::count);
        static constexpr std::size_t buttonCount = static_cast<std::size_t>(Button::count);

        void DrawAll();
        void DrawTitle();
        void DrawStatus(Item item);
        void DrawButton(Button button);
        void DrawBar(uint16_t y, uint16_t counts);
        void DrawTouchPad();
        void DrawTouchText();
        void DrawCursorSprite();
        void ShowLayers();
        void ApplyCursor();

    private:
        hal::DisplayController& display;
        hal::Blitter& blitter;
        hal::Surface frame;
        hal::Surface cursor;
        Canvas canvas;
        std::array<State, itemCount> states{};
        std::array<infra::BoundedString::WithStorage<24>, itemCount> details;
        std::array<bool, buttonCount> buttons{};
        uint16_t potentiometer{ 0 };
        uint16_t dac{ 0 };
        uint32_t uptime{ 0 };
        hal::TouchScreen::Phase touchPhase{ hal::TouchScreen::Phase::released };
        hal::TouchPoint touchPoint{ 0, 0 };
        bool ready{ false };
        bool committing{ false };
        bool cursorDirty{ false };
        bool cursorVisible{ false };
        std::size_t framesShown{ 0 };
        std::size_t underruns{ 0 };
    };
}
