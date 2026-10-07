#include "examples/display_demo/DisplayDemo.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace examples
{
    namespace
    {
        constexpr uint16_t spriteSide = 16;
        constexpr uint16_t maskSide = 32;
        constexpr uint16_t barWidth = 24;
        constexpr std::size_t framesPerBackground = 90;
        constexpr hal::Argb8888 barColor = 0xffffffff;
        constexpr hal::Argb8888 maskColor = 0xffff3030;
        constexpr hal::Argb8888 overlayColor = 0x80ff8000;
        constexpr std::array<hal::Argb8888, 4> backgrounds{ 0xff0b1d3a, 0xff1d3a0b, 0xff3a0b1d, 0xff2a2a2a };

        std::size_t SurfaceBytes(const hal::DisplayArea& area, hal::SurfaceFormat format)
        {
            return hal::BytesPerRow(area.width, format) * area.height;
        }

        hal::Surface MakeSurface(infra::ByteRange memory, const hal::DisplayArea& area, hal::SurfaceFormat format)
        {
            return { infra::Head(memory, SurfaceBytes(area, format)), hal::DisplaySize{ area.width, area.height }, static_cast<uint32_t>(hal::BytesPerRow(area.width, format)), format };
        }
    }

    std::size_t DisplayDemo::RequiredMemory(const Config& config)
    {
        return 2 * SurfaceBytes(config.window, config.windowFormat) + SurfaceBytes(config.overlay, hal::SurfaceFormat::argb8888);
    }

    DisplayDemo::DisplayDemo(hal::DisplayController& display, hal::Blitter& blitter, infra::ByteRange memory, const Config& config)
        : display(display)
        , blitter(blitter)
        , config(config)
    {
        std::size_t windowBytes = SurfaceBytes(config.window, config.windowFormat);

        really_assert(memory.size() >= RequiredMemory(config));
        really_assert(config.window.width > maskSide + barWidth && config.window.height > 3 * maskSide);
        really_assert(display.NumberOfLayers() >= 2);
        really_assert(blitter.Supports(hal::BlitOperation::fill, config.windowFormat, config.windowFormat));
        really_assert(blitter.Supports(hal::BlitOperation::fill, hal::SurfaceFormat::argb8888, hal::SurfaceFormat::argb8888));
        really_assert(blitter.Supports(hal::BlitOperation::copy, hal::SurfaceFormat::argb8888, config.windowFormat));

        buffers[0] = MakeSurface(memory, config.window, config.windowFormat);
        buffers[1] = MakeSurface(infra::DiscardHead(memory, windowBytes), config.window, config.windowFormat);
        overlay = MakeSurface(infra::DiscardHead(memory, 2 * windowBytes), config.overlay, hal::SurfaceFormat::argb8888);

        for (std::size_t y = 0; y != spriteSide; ++y)
            for (std::size_t x = 0; x != spriteSide; ++x)
                sprite[y * spriteSide + x] = ((x / 4 + y / 4) % 2) != 0 ? 0xffffcc00 : 0xff00ccff;

        for (int y = 0; y != maskSide; ++y)
            for (int x = 0; x != maskSide; ++x)
            {
                int dx = 2 * x - (maskSide - 1);
                int dy = 2 * y - (maskSide - 1);
                int distanceSquared = dx * dx + dy * dy;
                int radiusSquared = (maskSide - 1) * (maskSide - 1);
                mask[y * maskSide + x] = distanceSquared <= radiusSquared ? static_cast<uint8_t>(255 - 255 * distanceSquared / radiusSquared) : 0;
            }
    }

    void DisplayDemo::Start()
    {
        step = Step::prepareFront;
        RunStep();
    }

    std::size_t DisplayDemo::FramesShown() const
    {
        return framesShown;
    }

    std::size_t DisplayDemo::VerticalBlanks() const
    {
        return verticalBlanks;
    }

    std::size_t DisplayDemo::Underruns() const
    {
        return underruns;
    }

    void DisplayDemo::RunStep()
    {
        hal::Surface back = buffers[1 - front];

        switch (step)
        {
            case Step::prepareFront:
                blitter.Fill(buffers[front], backgrounds[0], StepDone());
                break;
            case Step::prepareBack:
                blitter.Fill(back, backgrounds[0], StepDone());
                break;
            case Step::prepareOverlay:
                blitter.Fill(overlay, overlayColor, StepDone());
                break;
            case Step::showInitial:
                ShowInitialLayers();
                break;
            case Step::clearBack:
                blitter.Fill(back, backgrounds[(frame / framesPerBackground) % backgrounds.size()], StepDone());
                break;
            case Step::drawBar:
                blitter.Fill(hal::SubSurface(back, BarArea()), barColor, StepDone());
                break;
            case Step::copySprite:
                blitter.Copy(hal::ConstSurface{ infra::ReinterpretCastByteRange(infra::MakeRange(sprite)), { spriteSide, spriteSide }, spriteSide * sizeof(uint32_t), hal::SurfaceFormat::argb8888 },
                    hal::SubSurface(back, SpriteArea()), StepDone());
                break;
            case Step::blendMask:
            {
                hal::Surface window = hal::SubSurface(back, MaskArea());
                hal::BlendSource source{ hal::ConstSurface{ infra::MakeRange(mask), { maskSide, maskSide }, maskSide, hal::SurfaceFormat::a8 }, 255, maskColor };

                if (blitter.Supports(hal::BlitOperation::blend, hal::SurfaceFormat::a8, config.windowFormat))
                    blitter.Blend(source, hal::AsConst(window), window, StepDone());
                else
                    NextStep();
                break;
            }
            case Step::present:
                Present();
                break;
        }
    }

    void DisplayDemo::NextStep()
    {
        switch (step)
        {
            case Step::prepareFront:
                step = Step::prepareBack;
                break;
            case Step::prepareBack:
                step = Step::prepareOverlay;
                break;
            case Step::prepareOverlay:
                step = Step::showInitial;
                break;
            case Step::showInitial:
            case Step::present:
                step = Step::clearBack;
                break;
            case Step::clearBack:
                step = Step::drawBar;
                break;
            case Step::drawBar:
                step = Step::copySprite;
                break;
            case Step::copySprite:
                step = Step::blendMask;
                break;
            case Step::blendMask:
                step = Step::present;
                break;
        }

        RunStep();
    }

    void DisplayDemo::ShowInitialLayers()
    {
        display.ConfigureLayer(0, { buffers[front], config.window.x, config.window.y, hal::BlendMode::constantAlpha, 255 });
        display.ConfigureLayer(1, { overlay, config.overlay.x, config.overlay.y, hal::BlendMode::pixelAlpha, 255 });

        display.Start(
            [this]()
            {
                ++verticalBlanks;
            },
            [this]()
            {
                ++underruns;
            });

        display.Commit([this]()
            {
                NextStep();
            });
    }

    void DisplayDemo::Present()
    {
        display.SetFramebuffer(0, buffers[1 - front].memory);
        display.Commit([this]()
            {
                FrameShown();
            });
    }

    void DisplayDemo::FrameShown()
    {
        front = 1 - front;
        ++framesShown;
        ++frame;
        NextStep();
    }

    infra::Function<void()> DisplayDemo::StepDone()
    {
        return [this]()
        {
            NextStep();
        };
    }

    hal::DisplayArea DisplayDemo::BarArea() const
    {
        uint16_t travel = config.window.width - barWidth;
        return { static_cast<uint16_t>((frame * 4) % travel), static_cast<uint16_t>(config.window.height / 4), barWidth, static_cast<uint16_t>(config.window.height / 2) };
    }

    hal::DisplayArea DisplayDemo::SpriteArea() const
    {
        uint16_t travel = config.window.width - spriteSide;
        return { static_cast<uint16_t>(travel - (frame * 2) % travel), static_cast<uint16_t>(config.window.height - spriteSide - 8), spriteSide, spriteSide };
    }

    hal::DisplayArea DisplayDemo::MaskArea() const
    {
        return { static_cast<uint16_t>((config.window.width - maskSide) / 2), 8, maskSide, maskSide };
    }
}
