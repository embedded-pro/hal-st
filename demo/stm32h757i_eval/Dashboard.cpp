#include "demo/stm32h757i_eval/Dashboard.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace main_
{
    namespace
    {
        constexpr hal::Argb8888 backgroundColor = 0xff101820;
        constexpr hal::Argb8888 panelColor = 0xff1c2733;
        constexpr hal::Argb8888 textColor = 0xffe8eef4;
        constexpr hal::Argb8888 dimColor = 0xff8fa3b5;
        constexpr hal::Argb8888 accentColor = 0xff3498db;
        constexpr hal::Argb8888 touchColor = 0xff2ecc71;
        constexpr hal::Argb8888 failedColor = 0xffe74c3c;
        constexpr hal::Argb8888 pendingColor = 0xfff1c40f;
        constexpr hal::Argb8888 cursorColor = 0xb0ffffff;
        constexpr hal::Argb8888 cursorRimColor = 0xffff7a00;

        constexpr uint16_t margin = 16;
        constexpr uint16_t statusTop = 56;
        constexpr uint16_t statusPitch = 34;
        constexpr uint16_t statusWidth = 420;
        constexpr uint16_t swatchSide = 24;
        constexpr uint16_t statusNameLeft = 52;
        constexpr uint16_t statusDetailLeft = 148;
        constexpr hal::DisplayArea touchPad{ 450, 56, 334, 332 };
        constexpr uint16_t touchDot = 10;
        constexpr uint16_t touchTextTop = 396;
        constexpr uint16_t barLabelLeft = 450;
        constexpr uint16_t barLeft = 496;
        constexpr uint16_t barWidth = 200;
        constexpr uint16_t barHeight = 24;
        constexpr uint16_t potentiometerBarTop = 418;
        constexpr uint16_t dacBarTop = 448;
        constexpr uint16_t adcMaximum = 4095;
        constexpr uint16_t buttonTop = 424;
        constexpr uint16_t buttonWidth = 54;
        constexpr uint16_t buttonHeight = 36;
        constexpr uint16_t buttonPitch = 60;
        constexpr uint16_t textScale = 2;
        constexpr uint16_t titleScale = 3;
        constexpr uint16_t rowTextOffset = 6;

        constexpr std::array<const char*, 10> itemNames{ "DISPLAY", "SDRAM", "SRAM", "NOR", "QSPI", "AUDIO", "TOUCH", "MFX", "ADC", "DAC" };
        constexpr std::array<const char*, 7> buttonNames{ "WKUP", "TAMP", "SEL", "UP", "DOWN", "LEFT", "RGHT" };

        hal::Argb8888 StateColor(Dashboard::State state)
        {
            switch (state)
            {
                case Dashboard::State::ok:
                    return touchColor;
                case Dashboard::State::failed:
                    return failedColor;
                default:
                    return pendingColor;
            }
        }

        hal::Surface MakeSurface(infra::ByteRange memory, hal::DisplaySize size, hal::SurfaceFormat format)
        {
            const auto stride = static_cast<uint32_t>(hal::BytesPerRow(size.width, format));
            return { infra::Head(memory, stride * size.height), size, stride, format };
        }
    }

    Dashboard::Dashboard(hal::DisplayController& display, hal::Blitter& blitter, infra::ByteRange frameMemory, infra::ByteRange cursorMemory)
        : display(display)
        , blitter(blitter)
        , frame(MakeSurface(frameMemory, screenSize, hal::SurfaceFormat::rgb565))
        , cursor(MakeSurface(cursorMemory, { cursorSide, cursorSide }, hal::SurfaceFormat::argb8888))
        , canvas(frame)
    {
        really_assert(frameMemory.size() >= frameBytes && cursorMemory.size() >= cursorBytes);
        really_assert(display.NumberOfLayers() >= 2);
        really_assert(blitter.Supports(hal::BlitOperation::fill, hal::SurfaceFormat::rgb565, hal::SurfaceFormat::rgb565));

        states.fill(State::pending);
        DrawCursorSprite();
    }

    void Dashboard::Start()
    {
        blitter.Fill(frame, backgroundColor, [this]()
            {
                DrawAll();
                ShowLayers();
            });
    }

    void Dashboard::SetStatus(Item item, State state, infra::BoundedConstString detail)
    {
        const auto index = static_cast<std::size_t>(item);

        states[index] = state;
        details[index].clear();
        details[index].append(detail.substr(0, details[index].max_size()));

        if (ready)
            DrawStatus(item);
    }

    void Dashboard::SetButton(Button button, bool pressed)
    {
        buttons[static_cast<std::size_t>(button)] = pressed;

        if (ready)
            DrawButton(button);
    }

    void Dashboard::SetPotentiometer(uint16_t counts)
    {
        potentiometer = counts;

        if (ready)
            DrawBar(potentiometerBarTop, counts);
    }

    void Dashboard::SetDac(uint16_t counts)
    {
        dac = counts;

        if (ready)
            DrawBar(dacBarTop, counts);
    }

    void Dashboard::SetUptime(uint32_t seconds)
    {
        uptime = seconds;

        if (ready)
            DrawTitle();
    }

    void Dashboard::SetTouch(hal::TouchScreen::Phase phase, hal::TouchPoint point)
    {
        touchPhase = phase;
        touchPoint = point;

        if (phase != hal::TouchScreen::Phase::released && ready)
        {
            const bool onPad = point.x >= touchPad.x + touchDot && point.x + touchDot < touchPad.x + touchPad.width && point.y >= touchPad.y + touchDot && point.y + touchDot < touchPad.y + touchPad.height;

            if (onPad)
                canvas.Fill({ static_cast<uint16_t>(point.x - touchDot / 2), static_cast<uint16_t>(point.y - touchDot / 2), touchDot, touchDot }, touchColor);
        }

        cursorVisible = phase != hal::TouchScreen::Phase::released;
        cursorDirty = true;

        if (ready)
        {
            DrawTouchText();
            ApplyCursor();
        }
    }

    void Dashboard::ClearTouchPad()
    {
        if (ready)
            DrawTouchPad();
    }

    std::size_t Dashboard::FramesShown() const
    {
        return framesShown;
    }

    std::size_t Dashboard::Underruns() const
    {
        return underruns;
    }

    void Dashboard::DrawAll()
    {
        DrawTitle();

        for (std::size_t index = 0; index != itemCount; ++index)
            DrawStatus(static_cast<Item>(index));

        for (std::size_t index = 0; index != buttonCount; ++index)
            DrawButton(static_cast<Button>(index));

        DrawTouchPad();
        DrawTouchText();

        canvas.Text(barLabelLeft, potentiometerBarTop + rowTextOffset - 2, textScale, "POT", dimColor, backgroundColor);
        canvas.Text(barLabelLeft, dacBarTop + rowTextOffset - 2, textScale, "DAC", dimColor, backgroundColor);
        DrawBar(potentiometerBarTop, potentiometer);
        DrawBar(dacBarTop, dac);
    }

    void Dashboard::DrawTitle()
    {
        canvas.Text(margin, 12, titleScale, "STM32H757I-EVAL HAL-ST DEMO", textColor, backgroundColor);

        infra::StringOutputStream::WithStorage<16> stream;
        stream << "UP " << infra::Width(2, '0') << uptime / 3600 << ":" << infra::Width(2, '0') << (uptime / 60) % 60 << ":" << infra::Width(2, '0') << uptime % 60;
        canvas.Text(screenSize.width - margin - Canvas::TextWidth(stream.Storage().size(), textScale), 18, textScale, stream.Storage(), dimColor, backgroundColor);
    }

    void Dashboard::DrawStatus(Item item)
    {
        const auto index = static_cast<std::size_t>(item);
        const uint16_t top = statusTop + static_cast<uint16_t>(index) * statusPitch;

        canvas.Fill({ margin, top, statusWidth, statusPitch - 2 }, backgroundColor);
        canvas.Fill({ margin, static_cast<uint16_t>(top + 2), swatchSide, swatchSide }, StateColor(states[index]));
        canvas.Text(statusNameLeft, top + rowTextOffset, textScale, itemNames[index], textColor, backgroundColor);
        canvas.Text(statusDetailLeft, top + rowTextOffset, textScale, details[index], dimColor, backgroundColor);
    }

    void Dashboard::DrawButton(Button button)
    {
        const auto index = static_cast<std::size_t>(button);
        const hal::DisplayArea area{ static_cast<uint16_t>(margin + index * buttonPitch), buttonTop, buttonWidth, buttonHeight };
        const hal::Argb8888 color = buttons[index] ? accentColor : panelColor;
        const char* name = buttonNames[index];
        const std::size_t length = infra::BoundedConstString(name).size();

        canvas.Fill(area, color);
        canvas.Text(area.x + (buttonWidth - Canvas::TextWidth(length, textScale)) / 2, area.y + (buttonHeight - Canvas::glyphHeight * textScale) / 2, textScale, name, textColor, color);
    }

    void Dashboard::DrawBar(uint16_t top, uint16_t counts)
    {
        const uint16_t filled = static_cast<uint16_t>(std::min<uint32_t>(counts, adcMaximum) * barWidth / adcMaximum);

        canvas.Fill({ barLeft, top, filled, barHeight }, accentColor);
        canvas.Fill({ static_cast<uint16_t>(barLeft + filled), top, static_cast<uint16_t>(barWidth - filled), barHeight }, panelColor);

        infra::StringOutputStream::WithStorage<8> stream;
        stream << infra::Width(4, ' ') << counts;
        canvas.Text(barLeft + barWidth + 8, top + rowTextOffset - 2, textScale, stream.Storage(), textColor, backgroundColor);
    }

    void Dashboard::DrawTouchPad()
    {
        canvas.Fill(touchPad, panelColor);
        canvas.Frame(touchPad, 2, accentColor);
    }

    void Dashboard::DrawTouchText()
    {
        infra::StringOutputStream::WithStorage<28> stream;

        if (touchPhase == hal::TouchScreen::Phase::released)
            stream << "TOUCH RELEASED";
        else
            stream << "TOUCH " << infra::Width(3, ' ') << touchPoint.x << "," << infra::Width(3, ' ') << touchPoint.y;

        while (stream.Storage().size() < 24)
            stream << " ";

        canvas.Text(barLabelLeft, touchTextTop, textScale, stream.Storage(), textColor, backgroundColor);
    }

    void Dashboard::DrawCursorSprite()
    {
        auto* pixels = reinterpret_cast<uint32_t*>(cursor.memory.begin());
        const int centre = cursorSide - 1;

        for (int y = 0; y != cursorSide; ++y)
            for (int x = 0; x != cursorSide; ++x)
            {
                const int dx = 2 * x - centre;
                const int dy = 2 * y - centre;
                const int distanceSquared = dx * dx + dy * dy;
                const int radiusSquared = centre * centre;

                if (distanceSquared > radiusSquared)
                    pixels[y * cursorSide + x] = 0;
                else if (distanceSquared > radiusSquared * 3 / 4)
                    pixels[y * cursorSide + x] = cursorRimColor;
                else
                    pixels[y * cursorSide + x] = cursorColor;
            }
    }

    void Dashboard::ShowLayers()
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
                ready = true;
                ApplyCursor();
            });
    }

    void Dashboard::ApplyCursor()
    {
        if (!ready || committing || !cursorDirty)
            return;

        cursorDirty = false;

        if (cursorVisible)
        {
            const uint16_t x = static_cast<uint16_t>(std::clamp<int>(touchPoint.x - cursorSide / 2, 0, screenSize.width - cursorSide));
            const uint16_t y = static_cast<uint16_t>(std::clamp<int>(touchPoint.y - cursorSide / 2, 0, screenSize.height - cursorSide));
            display.ConfigureLayer(1, { cursor, x, y, hal::BlendMode::pixelAlpha, 255 });
        }
        else
            display.DisableLayer(1);

        committing = true;
        display.Commit([this]()
            {
                committing = false;
                ApplyCursor();
            });
    }
}
