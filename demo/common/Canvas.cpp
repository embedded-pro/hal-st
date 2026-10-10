#include "demo/common/Canvas.hpp"
#include <algorithm>
#include <array>

namespace main_
{
    namespace
    {
        constexpr char firstGlyph = ' ';
        constexpr char lastGlyph = 'Z';
        constexpr uint16_t glyphColumns = 5;

        // 5x7 columns, bit 0 is the top row
        constexpr std::array<std::array<uint8_t, glyphColumns>, lastGlyph - firstGlyph + 1> font{ { { 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x5f, 0x00, 0x00 },
            { 0x00, 0x07, 0x00, 0x07, 0x00 },
            { 0x14, 0x7f, 0x14, 0x7f, 0x14 },
            { 0x24, 0x2a, 0x7f, 0x2a, 0x12 },
            { 0x23, 0x13, 0x08, 0x64, 0x62 },
            { 0x36, 0x49, 0x56, 0x20, 0x50 },
            { 0x00, 0x08, 0x07, 0x03, 0x00 },
            { 0x00, 0x1c, 0x22, 0x41, 0x00 },
            { 0x00, 0x41, 0x22, 0x1c, 0x00 },
            { 0x2a, 0x1c, 0x7f, 0x1c, 0x2a },
            { 0x08, 0x08, 0x3e, 0x08, 0x08 },
            { 0x00, 0x50, 0x30, 0x00, 0x00 },
            { 0x08, 0x08, 0x08, 0x08, 0x08 },
            { 0x00, 0x00, 0x60, 0x60, 0x00 },
            { 0x20, 0x10, 0x08, 0x04, 0x02 },
            { 0x3e, 0x51, 0x49, 0x45, 0x3e },
            { 0x00, 0x42, 0x7f, 0x40, 0x00 },
            { 0x72, 0x49, 0x49, 0x49, 0x46 },
            { 0x21, 0x41, 0x49, 0x4d, 0x33 },
            { 0x18, 0x14, 0x12, 0x7f, 0x10 },
            { 0x27, 0x45, 0x45, 0x45, 0x39 },
            { 0x3c, 0x4a, 0x49, 0x49, 0x31 },
            { 0x41, 0x21, 0x11, 0x09, 0x07 },
            { 0x36, 0x49, 0x49, 0x49, 0x36 },
            { 0x46, 0x49, 0x49, 0x29, 0x1e },
            { 0x00, 0x00, 0x14, 0x00, 0x00 },
            { 0x00, 0x40, 0x34, 0x00, 0x00 },
            { 0x00, 0x08, 0x14, 0x22, 0x41 },
            { 0x14, 0x14, 0x14, 0x14, 0x14 },
            { 0x00, 0x41, 0x22, 0x14, 0x08 },
            { 0x02, 0x01, 0x59, 0x09, 0x06 },
            { 0x3e, 0x41, 0x5d, 0x59, 0x4e },
            { 0x7c, 0x12, 0x11, 0x12, 0x7c },
            { 0x7f, 0x49, 0x49, 0x49, 0x36 },
            { 0x3e, 0x41, 0x41, 0x41, 0x22 },
            { 0x7f, 0x41, 0x41, 0x41, 0x3e },
            { 0x7f, 0x49, 0x49, 0x49, 0x41 },
            { 0x7f, 0x09, 0x09, 0x09, 0x01 },
            { 0x3e, 0x41, 0x41, 0x51, 0x73 },
            { 0x7f, 0x08, 0x08, 0x08, 0x7f },
            { 0x00, 0x41, 0x7f, 0x41, 0x00 },
            { 0x20, 0x40, 0x41, 0x3f, 0x01 },
            { 0x7f, 0x08, 0x14, 0x22, 0x41 },
            { 0x7f, 0x40, 0x40, 0x40, 0x40 },
            { 0x7f, 0x02, 0x1c, 0x02, 0x7f },
            { 0x7f, 0x04, 0x08, 0x10, 0x7f },
            { 0x3e, 0x41, 0x41, 0x41, 0x3e },
            { 0x7f, 0x09, 0x09, 0x09, 0x06 },
            { 0x3e, 0x41, 0x51, 0x21, 0x5e },
            { 0x7f, 0x09, 0x19, 0x29, 0x46 },
            { 0x26, 0x49, 0x49, 0x49, 0x32 },
            { 0x03, 0x01, 0x7f, 0x01, 0x03 },
            { 0x3f, 0x40, 0x40, 0x40, 0x3f },
            { 0x1f, 0x20, 0x40, 0x20, 0x1f },
            { 0x3f, 0x40, 0x38, 0x40, 0x3f },
            { 0x63, 0x14, 0x08, 0x14, 0x63 },
            { 0x03, 0x04, 0x78, 0x04, 0x03 },
            { 0x61, 0x59, 0x49, 0x4d, 0x43 } } };

        const std::array<uint8_t, glyphColumns>& Glyph(char character)
        {
            if (character >= 'a' && character <= 'z')
                character = static_cast<char>(character - 'a' + 'A');

            if (character < firstGlyph || character > lastGlyph)
                character = '?';

            return font[character - firstGlyph];
        }
    }

    Canvas::Canvas(const hal::Surface& surface)
        : surface(surface)
    {
        really_assert(surface.format == hal::SurfaceFormat::rgb565 && hal::IsValidSurface(surface));
    }

    void Canvas::Fill(const hal::DisplayArea& area, hal::Argb8888 color)
    {
        hal::DisplayArea clipped = Clip(area);
        uint16_t pixel = static_cast<uint16_t>(hal::ToPixel(color, hal::SurfaceFormat::rgb565));

        for (uint16_t y = clipped.y; y != clipped.y + clipped.height; ++y)
            std::fill_n(Row(y) + clipped.x, clipped.width, pixel);
    }

    void Canvas::Frame(const hal::DisplayArea& area, uint16_t thickness, hal::Argb8888 color)
    {
        Fill({ area.x, area.y, area.width, thickness }, color);
        Fill({ area.x, static_cast<uint16_t>(area.y + area.height - thickness), area.width, thickness }, color);
        Fill({ area.x, area.y, thickness, area.height }, color);
        Fill({ static_cast<uint16_t>(area.x + area.width - thickness), area.y, thickness, area.height }, color);
    }

    void Canvas::Text(uint16_t x, uint16_t y, uint16_t scale, infra::BoundedConstString text, hal::Argb8888 foreground, hal::Argb8888 background)
    {
        const uint16_t foregroundPixel = static_cast<uint16_t>(hal::ToPixel(foreground, hal::SurfaceFormat::rgb565));
        const uint16_t backgroundPixel = static_cast<uint16_t>(hal::ToPixel(background, hal::SurfaceFormat::rgb565));

        for (std::size_t index = 0; index != text.size(); ++index)
        {
            const auto& glyph = Glyph(text[index]);
            const uint32_t cellX = x + index * glyphWidth * scale;

            if (cellX + glyphWidth * scale > surface.size.width || y + glyphHeight * scale > surface.size.height)
                return;

            for (uint16_t row = 0; row != glyphHeight; ++row)
                for (uint16_t line = 0; line != scale; ++line)
                {
                    uint16_t* destination = Row(static_cast<uint16_t>(y + row * scale + line)) + cellX;

                    for (uint16_t column = 0; column != glyphWidth; ++column)
                    {
                        const bool set = column < glyphColumns && row < glyphHeight - 1 && (glyph[column] & (1u << row)) != 0;
                        std::fill_n(destination + column * scale, scale, set ? foregroundPixel : backgroundPixel);
                    }
                }
        }
    }

    uint16_t Canvas::TextWidth(std::size_t characters, uint16_t scale)
    {
        return static_cast<uint16_t>(characters * glyphWidth * scale);
    }

    hal::DisplayArea Canvas::Clip(const hal::DisplayArea& area) const
    {
        const uint16_t x = std::min(area.x, surface.size.width);
        const uint16_t y = std::min(area.y, surface.size.height);

        return { x, y, std::min<uint16_t>(area.width, surface.size.width - x), std::min<uint16_t>(area.height, surface.size.height - y) };
    }

    uint16_t* Canvas::Row(uint16_t y) const
    {
        return reinterpret_cast<uint16_t*>(surface.memory.begin() + static_cast<std::size_t>(y) * surface.strideInBytes);
    }
}
