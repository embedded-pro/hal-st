#include "demo/common/TouchCursor.hpp"
#include "demo/common/Surfaces.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace main_
{
    namespace
    {
        constexpr hal::Argb8888 cursorColor = 0xb0ffffff;
        constexpr hal::Argb8888 rimColor = 0xffff7a00;
    }

    TouchCursor::TouchCursor(hal::DisplayController& display, std::size_t layer, infra::ByteRange memory)
        : display(display)
        , layer(layer)
        , sprite(MakeSurface(memory, { side, side }, hal::SurfaceFormat::argb8888))
    {
        really_assert(memory.size() >= bytes);
        really_assert(layer < display.NumberOfLayers());

        DrawSprite();
    }

    void TouchCursor::Enable()
    {
        enabled = true;
        Apply();
    }

    void TouchCursor::Show(hal::TouchPoint point)
    {
        this->point = point;
        visible = true;
        dirty = true;
        Apply();
    }

    void TouchCursor::Hide()
    {
        visible = false;
        dirty = true;
        Apply();
    }

    void TouchCursor::DrawSprite()
    {
        auto* pixels = reinterpret_cast<uint32_t*>(sprite.memory.begin());
        const int centre = side - 1;

        for (int y = 0; y != side; ++y)
            for (int x = 0; x != side; ++x)
            {
                const int dx = 2 * x - centre;
                const int dy = 2 * y - centre;
                const int distanceSquared = dx * dx + dy * dy;
                const int radiusSquared = centre * centre;

                if (distanceSquared > radiusSquared)
                    pixels[y * side + x] = 0;
                else if (distanceSquared > radiusSquared * 3 / 4)
                    pixels[y * side + x] = rimColor;
                else
                    pixels[y * side + x] = cursorColor;
            }
    }

    void TouchCursor::Apply()
    {
        if (!enabled || committing || !dirty)
            return;

        dirty = false;

        if (visible)
        {
            const hal::DisplaySize screen = display.Size();
            const uint16_t x = static_cast<uint16_t>(std::clamp<int>(point.x - side / 2, 0, screen.width - side));
            const uint16_t y = static_cast<uint16_t>(std::clamp<int>(point.y - side / 2, 0, screen.height - side));
            display.ConfigureLayer(layer, { sprite, x, y, hal::BlendMode::pixelAlpha, 255 });
        }
        else
            display.DisableLayer(layer);

        committing = true;
        display.Commit([this]()
            {
                committing = false;
                Apply();
            });
    }
}
