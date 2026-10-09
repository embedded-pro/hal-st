#pragma once

#include "hal/interfaces/Surface.hpp"
#include "infra/util/BoundedString.hpp"
#include <cstdint>

namespace main_
{
    class Canvas
    {
    public:
        static constexpr uint16_t glyphWidth = 6;
        static constexpr uint16_t glyphHeight = 8;

        explicit Canvas(const hal::Surface& surface);

        void Fill(const hal::DisplayArea& area, hal::Argb8888 color);
        void Frame(const hal::DisplayArea& area, uint16_t thickness, hal::Argb8888 color);
        void Text(uint16_t x, uint16_t y, uint16_t scale, infra::BoundedConstString text, hal::Argb8888 foreground, hal::Argb8888 background);

        static uint16_t TextWidth(std::size_t characters, uint16_t scale);

    private:
        hal::DisplayArea Clip(const hal::DisplayArea& area) const;
        uint16_t* Row(uint16_t y) const;

    private:
        hal::Surface surface;
    };
}
