#include "demo/stm32h745i_disco/DiscoveryDashboard.hpp"
#include "demo/common/Surfaces.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace main_
{
    namespace
    {
        constexpr uint16_t margin = 8;
        constexpr uint16_t titleTop = 6;
        constexpr uint16_t statusTop = 30;
        constexpr uint16_t statusPitch = 22;
        constexpr uint16_t swatchSide = 16;
        constexpr uint16_t statusNameLeft = 32;
        constexpr uint16_t statusDetailLeft = 120;
        constexpr uint16_t rowTextOffset = 3;
        constexpr hal::DisplayArea button{ margin, 144, 64, 24 };
        constexpr uint16_t touchTextLeft = 84;
        constexpr uint16_t touchTextTop = 148;
        constexpr std::size_t touchTextWidth = 24;
        constexpr hal::DisplayArea touchPad{ margin, 174, 464, 90 };
        constexpr uint16_t touchDot = 8;
        constexpr uint16_t textScale = 2;

        constexpr std::array<const char*, static_cast<std::size_t>(DiscoveryDashboard::Item::count)> itemNames{ "DISPLAY", "SDRAM", "QSPI", "AUDIO", "TOUCH" };

        bool OnPad(hal::TouchPoint point)
        {
            return point.x >= touchPad.x + touchDot && point.x + touchDot < touchPad.x + touchPad.width && point.y >= touchPad.y + touchDot && point.y + touchDot < touchPad.y + touchPad.height;
        }
    }

    DiscoveryDashboard::DiscoveryDashboard(hal::DisplayController& display, hal::Blitter& blitter, infra::ByteRange frameMemory, infra::ByteRange cursorMemory)
        : display(display)
        , blitter(blitter)
        , frame(MakeSurface(frameMemory, screenSize, hal::SurfaceFormat::rgb565))
        , canvas(frame)
        , cursor(display, 1, cursorMemory)
    {
        really_assert(frameMemory.size() >= frameBytes);
        really_assert(display.Size() == screenSize);
        really_assert(blitter.Supports(hal::BlitOperation::fill, hal::SurfaceFormat::rgb565, hal::SurfaceFormat::rgb565));

        states.fill(State::pending);
    }

    void DiscoveryDashboard::Start(const infra::Function<void()>& onStarted)
    {
        this->onStarted = onStarted;

        blitter.Fill(frame, style::backgroundColor, [this]()
            {
                DrawAll();
                drawn = true;
                ShowLayers();
            });
    }

    void DiscoveryDashboard::SetStatus(Item item, State state, infra::BoundedConstString detail)
    {
        const auto index = static_cast<std::size_t>(item);

        states[index] = state;
        details[index].clear();
        details[index].append(detail.substr(0, details[index].max_size()));

        if (drawn)
            DrawStatus(item);
    }

    void DiscoveryDashboard::SetButton(bool pressed)
    {
        buttonPressed = pressed;

        if (drawn)
            DrawButton();
    }

    void DiscoveryDashboard::SetUptime(uint32_t seconds)
    {
        uptime = seconds;

        if (drawn)
            DrawTitle();
    }

    void DiscoveryDashboard::SetTouch(hal::TouchScreen::Phase phase, hal::TouchPoint point)
    {
        touchPhase = phase;
        touchPoint = point;

        if (phase == hal::TouchScreen::Phase::released)
            cursor.Hide();
        else
        {
            cursor.Show(point);

            if (drawn)
                PaintTouchDot(point);
        }

        if (drawn)
            DrawTouchText();
    }

    void DiscoveryDashboard::ClearTouchPad()
    {
        if (drawn)
            DrawTouchPad();
    }

    std::size_t DiscoveryDashboard::FramesShown() const
    {
        return framesShown;
    }

    std::size_t DiscoveryDashboard::Underruns() const
    {
        return underruns;
    }

    void DiscoveryDashboard::DrawAll()
    {
        DrawTitle();

        for (std::size_t index = 0; index != itemCount; ++index)
            DrawStatus(static_cast<Item>(index));

        DrawButton();
        DrawTouchText();
        DrawTouchPad();
    }

    void DiscoveryDashboard::DrawTitle()
    {
        canvas.Text(margin, titleTop, textScale, "STM32H745I-DISCO DEMO", style::textColor, style::backgroundColor);

        infra::StringOutputStream::WithStorage<16> stream;
        stream << "UP " << infra::Width(2, '0') << uptime / 3600 << ":" << infra::Width(2, '0') << (uptime / 60) % 60 << ":" << infra::Width(2, '0') << uptime % 60;
        canvas.Text(screenSize.width - margin - Canvas::TextWidth(stream.Storage().size(), textScale), titleTop, textScale, stream.Storage(), style::dimColor, style::backgroundColor);
    }

    void DiscoveryDashboard::DrawStatus(Item item)
    {
        const auto index = static_cast<std::size_t>(item);
        const uint16_t top = statusTop + static_cast<uint16_t>(index) * statusPitch;

        canvas.Fill({ margin, top, screenSize.width - 2 * margin, statusPitch - 2 }, style::backgroundColor);
        canvas.Fill({ margin, static_cast<uint16_t>(top + 1), swatchSide, swatchSide }, style::StateColor(states[index]));
        canvas.Text(statusNameLeft, top + rowTextOffset, textScale, itemNames[index], style::textColor, style::backgroundColor);
        canvas.Text(statusDetailLeft, top + rowTextOffset, textScale, details[index], style::dimColor, style::backgroundColor);
    }

    void DiscoveryDashboard::DrawButton()
    {
        const infra::BoundedConstString name = "USER";
        const hal::Argb8888 color = buttonPressed ? style::accentColor : style::panelColor;

        canvas.Fill(button, color);
        canvas.Text(button.x + (button.width - Canvas::TextWidth(name.size(), textScale)) / 2, button.y + (button.height - Canvas::glyphHeight * textScale) / 2, textScale, name, style::textColor, color);
    }

    void DiscoveryDashboard::DrawTouchPad()
    {
        canvas.Fill(touchPad, style::panelColor);
        canvas.Frame(touchPad, 2, style::accentColor);
    }

    void DiscoveryDashboard::DrawTouchText()
    {
        infra::StringOutputStream::WithStorage<28> stream;

        if (touchPhase == hal::TouchScreen::Phase::released)
            stream << "TOUCH RELEASED";
        else
            stream << "TOUCH " << infra::Width(3, ' ') << touchPoint.x << "," << infra::Width(3, ' ') << touchPoint.y;

        while (stream.Storage().size() < touchTextWidth)
            stream << " ";

        canvas.Text(touchTextLeft, touchTextTop, textScale, stream.Storage(), style::textColor, style::backgroundColor);
    }

    void DiscoveryDashboard::PaintTouchDot(hal::TouchPoint point)
    {
        if (OnPad(point))
            canvas.Fill({ static_cast<uint16_t>(point.x - touchDot / 2), static_cast<uint16_t>(point.y - touchDot / 2), touchDot, touchDot }, style::okColor);
    }

    void DiscoveryDashboard::ShowLayers()
    {
        display.ConfigureLayer(0, { frame, 0, 0, hal::BlendMode::constantAlpha, 255 });

        display.Start(
            [this]()
            {
                ++framesShown;
            },
            [this]()
            {
                ++underruns;
            });

        display.Commit([this]()
            {
                cursor.Enable();
                this->onStarted();
            });
    }
}
