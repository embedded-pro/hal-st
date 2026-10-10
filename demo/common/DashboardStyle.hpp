#pragma once

#include "hal/interfaces/Surface.hpp"
#include <cstdint>

namespace main_
{
    enum class StatusState : uint8_t
    {
        pending,
        ok,
        failed
    };

    namespace style
    {
        constexpr hal::Argb8888 backgroundColor = 0xff101820;
        constexpr hal::Argb8888 panelColor = 0xff1c2733;
        constexpr hal::Argb8888 textColor = 0xffe8eef4;
        constexpr hal::Argb8888 dimColor = 0xff8fa3b5;
        constexpr hal::Argb8888 accentColor = 0xff3498db;
        constexpr hal::Argb8888 okColor = 0xff2ecc71;
        constexpr hal::Argb8888 failedColor = 0xffe74c3c;
        constexpr hal::Argb8888 pendingColor = 0xfff1c40f;

        constexpr hal::Argb8888 StateColor(StatusState state)
        {
            switch (state)
            {
                case StatusState::ok:
                    return okColor;
                case StatusState::failed:
                    return failedColor;
                default:
                    return pendingColor;
            }
        }
    }
}
