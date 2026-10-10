#include "demo/stm32h757i_eval/Dashboard.hpp"
#include "demo/common/Surfaces.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace main_
{
    namespace
    {
        constexpr uint16_t margin = 16;
        constexpr uint16_t statusTop = 56;
        constexpr uint16_t statusPitch = 32;
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

        constexpr std::array<const char*, static_cast<std::size_t>(Dashboard::Item::count)> itemNames{ "DISPLAY", "SDRAM", "SRAM", "NOR", "QSPI", "SDCARD", "AUDIO", "TOUCH", "MFX", "ADC", "DAC" };
        constexpr std::array<const char*, 7> buttonNames{ "WKUP", "TAMP", "SEL", "UP", "DOWN", "LEFT", "RGHT" };
    }

    Dashboard::Dashboard(hal::DisplayController& display, hal::Blitter& blitter, infra::ByteRange frameMemory, infra::ByteRange cursorMemory)
        : display(display)
        , blitter(blitter)
        , frame(MakeSurface(frameMemory, screenSize, hal::SurfaceFormat::rgb565))
        , canvas(frame)
        , cursor(display, 1, cursorMemory)
    {
        really_assert(frameMemory.size() >= frameBytes);
        really_assert(blitter.Supports(hal::BlitOperation::fill, hal::SurfaceFormat::rgb565, hal::SurfaceFormat::rgb565));

        states.fill(State::pending);
    }

    void Dashboard::Start(const infra::Function<void()>& onStarted)
    {
        this->onStarted = onStarted;

        blitter.Fill(frame, style::backgroundColor, [this]()
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
                canvas.Fill({ static_cast<uint16_t>(point.x - touchDot / 2), static_cast<uint16_t>(point.y - touchDot / 2), touchDot, touchDot }, style::okColor);
        }

        if (phase == hal::TouchScreen::Phase::released)
            cursor.Hide();
        else
            cursor.Show(point);

        if (ready)
            DrawTouchText();
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

        canvas.Text(barLabelLeft, potentiometerBarTop + rowTextOffset - 2, textScale, "POT", style::dimColor, style::backgroundColor);
        canvas.Text(barLabelLeft, dacBarTop + rowTextOffset - 2, textScale, "DAC", style::dimColor, style::backgroundColor);
        DrawBar(potentiometerBarTop, potentiometer);
        DrawBar(dacBarTop, dac);
    }

    void Dashboard::DrawTitle()
    {
        canvas.Text(margin, 12, titleScale, "STM32H757I-EVAL HAL-ST DEMO", style::textColor, style::backgroundColor);

        infra::StringOutputStream::WithStorage<16> stream;
        stream << "UP " << infra::Width(2, '0') << uptime / 3600 << ":" << infra::Width(2, '0') << (uptime / 60) % 60 << ":" << infra::Width(2, '0') << uptime % 60;
        canvas.Text(screenSize.width - margin - Canvas::TextWidth(stream.Storage().size(), textScale), 18, textScale, stream.Storage(), style::dimColor, style::backgroundColor);
    }

    void Dashboard::DrawStatus(Item item)
    {
        const auto index = static_cast<std::size_t>(item);
        const uint16_t top = statusTop + static_cast<uint16_t>(index) * statusPitch;

        canvas.Fill({ margin, top, statusWidth, statusPitch - 2 }, style::backgroundColor);
        canvas.Fill({ margin, static_cast<uint16_t>(top + 2), swatchSide, swatchSide }, style::StateColor(states[index]));
        canvas.Text(statusNameLeft, top + rowTextOffset, textScale, itemNames[index], style::textColor, style::backgroundColor);
        canvas.Text(statusDetailLeft, top + rowTextOffset, textScale, details[index], style::dimColor, style::backgroundColor);
    }

    void Dashboard::DrawButton(Button button)
    {
        const auto index = static_cast<std::size_t>(button);
        const hal::DisplayArea area{ static_cast<uint16_t>(margin + index * buttonPitch), buttonTop, buttonWidth, buttonHeight };
        const hal::Argb8888 color = buttons[index] ? style::accentColor : style::panelColor;
        const char* name = buttonNames[index];
        const std::size_t length = infra::BoundedConstString(name).size();

        canvas.Fill(area, color);
        canvas.Text(area.x + (buttonWidth - Canvas::TextWidth(length, textScale)) / 2, area.y + (buttonHeight - Canvas::glyphHeight * textScale) / 2, textScale, name, style::textColor, color);
    }

    void Dashboard::DrawBar(uint16_t top, uint16_t counts)
    {
        const uint16_t filled = static_cast<uint16_t>(std::min<uint32_t>(counts, adcMaximum) * barWidth / adcMaximum);

        canvas.Fill({ barLeft, top, filled, barHeight }, style::accentColor);
        canvas.Fill({ static_cast<uint16_t>(barLeft + filled), top, static_cast<uint16_t>(barWidth - filled), barHeight }, style::panelColor);

        infra::StringOutputStream::WithStorage<8> stream;
        stream << infra::Width(4, ' ') << counts;
        canvas.Text(barLeft + barWidth + 8, top + rowTextOffset - 2, textScale, stream.Storage(), style::textColor, style::backgroundColor);
    }

    void Dashboard::DrawTouchPad()
    {
        canvas.Fill(touchPad, style::panelColor);
        canvas.Frame(touchPad, 2, style::accentColor);
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

        canvas.Text(barLabelLeft, touchTextTop, textScale, stream.Storage(), style::textColor, style::backgroundColor);
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
                cursor.Enable();
                this->onStarted();
            });
    }
}
