#pragma once

#include "hal/interfaces/DisplayController.hpp"
#include "hal/interfaces/Surface.hpp"
#include "hal/interfaces/TouchScreen.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>

namespace main_
{
    class TouchCursor
    {
    public:
        static constexpr uint16_t side = 28;
        static constexpr std::size_t bytes = side * side * 4;

        TouchCursor(hal::DisplayController& display, std::size_t layer, infra::ByteRange memory);

        void Enable();
        void Show(hal::TouchPoint point);
        void Hide();

    private:
        void DrawSprite();
        void Apply();

    private:
        hal::DisplayController& display;
        std::size_t layer;
        hal::Surface sprite;
        hal::TouchPoint point{ 0, 0 };
        bool enabled{ false };
        bool visible{ false };
        bool dirty{ false };
        bool committing{ false };
    };
}
