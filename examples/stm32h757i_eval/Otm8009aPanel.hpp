#pragma once

#include "drivers/display/mipi_dsi/MipiDsiPanelCore.hpp"
#include "hal/interfaces/Display.hpp"
#include <array>
#include <chrono>
#include <cstdint>

// The KoD KM-040TMP-02-0621 4" WVGA panel with an OTM8009A on the DSI link of the STM32H757I-EVAL.
// The commands and parameters are derived from otm8009a.c of the STM32H747I-EVAL board support package
// of STMicroelectronics, Copyright 2015 STMicroelectronics, BSD 3-Clause. The panel is 480 x 800 by itself and is used in landscape
namespace main_::otm8009a
{
    using Command = drivers::MipiDsiPanelCore::Command;
    using Packet = drivers::MipiDsiPanelCore::Packet;

    inline constexpr hal::DisplaySize landscape{ 800, 480 };
    inline constexpr uint8_t landscapeAddressMode = 0x60;

    inline constexpr std::array<uint8_t, 1> parameters0{ 0x00 };
    inline constexpr std::array<uint8_t, 3> parameters1{ 0x80, 0x09, 0x01 };
    inline constexpr std::array<uint8_t, 1> parameters2{ 0x80 };
    inline constexpr std::array<uint8_t, 2> parameters3{ 0x80, 0x09 };
    inline constexpr std::array<uint8_t, 1> parameters4{ 0x30 };
    inline constexpr std::array<uint8_t, 1> parameters5{ 0x8a };
    inline constexpr std::array<uint8_t, 1> parameters6{ 0x40 };
    inline constexpr std::array<uint8_t, 1> parameters7{ 0xb1 };
    inline constexpr std::array<uint8_t, 1> parameters8{ 0xa9 };
    inline constexpr std::array<uint8_t, 1> parameters9{ 0x91 };
    inline constexpr std::array<uint8_t, 1> parameters10{ 0x34 };
    inline constexpr std::array<uint8_t, 1> parameters11{ 0xb4 };
    inline constexpr std::array<uint8_t, 1> parameters12{ 0x50 };
    inline constexpr std::array<uint8_t, 1> parameters13{ 0x4e };
    inline constexpr std::array<uint8_t, 1> parameters14{ 0x81 };
    inline constexpr std::array<uint8_t, 1> parameters15{ 0x66 };
    inline constexpr std::array<uint8_t, 1> parameters16{ 0xa1 };
    inline constexpr std::array<uint8_t, 1> parameters17{ 0x08 };
    inline constexpr std::array<uint8_t, 1> parameters18{ 0x92 };
    inline constexpr std::array<uint8_t, 1> parameters19{ 0x01 };
    inline constexpr std::array<uint8_t, 1> parameters20{ 0x95 };
    inline constexpr std::array<uint8_t, 2> parameters21{ 0x79, 0x79 };
    inline constexpr std::array<uint8_t, 1> parameters22{ 0x94 };
    inline constexpr std::array<uint8_t, 1> parameters23{ 0x33 };
    inline constexpr std::array<uint8_t, 1> parameters24{ 0xa3 };
    inline constexpr std::array<uint8_t, 1> parameters25{ 0x1b };
    inline constexpr std::array<uint8_t, 1> parameters26{ 0x82 };
    inline constexpr std::array<uint8_t, 1> parameters27{ 0x83 };
    inline constexpr std::array<uint8_t, 1> parameters28{ 0x0e };
    inline constexpr std::array<uint8_t, 1> parameters29{ 0xa6 };
    inline constexpr std::array<uint8_t, 2> parameters30{ 0x00, 0x01 };
    inline constexpr std::array<uint8_t, 6> parameters31{ 0x85, 0x01, 0x00, 0x84, 0x01, 0x00 };
    inline constexpr std::array<uint8_t, 1> parameters32{ 0xa0 };
    inline constexpr std::array<uint8_t, 14> parameters33{ 0x18, 0x04, 0x03, 0x39, 0x00, 0x00, 0x00, 0x18, 0x03, 0x03, 0x3a, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 1> parameters34{ 0xb0 };
    inline constexpr std::array<uint8_t, 14> parameters35{ 0x18, 0x02, 0x03, 0x3b, 0x00, 0x00, 0x00, 0x18, 0x01, 0x03, 0x3c, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 1> parameters36{ 0xc0 };
    inline constexpr std::array<uint8_t, 10> parameters37{ 0x01, 0x01, 0x20, 0x20, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 1> parameters38{ 0xd0 };
    inline constexpr std::array<uint8_t, 10> parameters39{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 1> parameters40{ 0x90 };
    inline constexpr std::array<uint8_t, 15> parameters41{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 15> parameters42{ 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 15> parameters43{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 1> parameters44{ 0xe0 };
    inline constexpr std::array<uint8_t, 1> parameters45{ 0xf0 };
    inline constexpr std::array<uint8_t, 10> parameters46{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    inline constexpr std::array<uint8_t, 10> parameters47{ 0x00, 0x26, 0x09, 0x0b, 0x01, 0x25, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 15> parameters48{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0x0a, 0x0c, 0x02 };
    inline constexpr std::array<uint8_t, 15> parameters49{ 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 10> parameters50{ 0x00, 0x25, 0x0c, 0x0a, 0x02, 0x26, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 15> parameters51{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x25, 0x0b, 0x09, 0x01 };
    inline constexpr std::array<uint8_t, 15> parameters52{ 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    inline constexpr std::array<uint8_t, 1> parameters53{ 0xb6 };
    inline constexpr std::array<uint8_t, 1> parameters54{ 0x06 };
    inline constexpr std::array<uint8_t, 3> parameters55{ 0xff, 0xff, 0xff };
    inline constexpr std::array<uint8_t, 16> parameters56{ 0x00, 0x09, 0x0f, 0x0e, 0x07, 0x10, 0x0b, 0x0a, 0x04, 0x07, 0x0b, 0x08, 0x0f, 0x10, 0x0a, 0x01 };
    inline constexpr std::array<uint8_t, 4> parameters57{ 0x00, 0x00, 0x03, 0x1f };
    inline constexpr std::array<uint8_t, 4> parameters58{ 0x00, 0x00, 0x01, 0xdf };
    inline constexpr std::array<uint8_t, 1> parameters59{ 0x7f };
    inline constexpr std::array<uint8_t, 1> parameters60{ 0x2c };
    inline constexpr std::array<uint8_t, 1> parameters61{ 0x02 };
    inline constexpr std::array<uint8_t, 1> parameters62{ 0xff };

    inline constexpr std::array<Command, 89> beforeSleepOut{ {
        { Packet::dcs, 0x00, parameters0, 0 },
        { Packet::dcs, 0xff, parameters1, 0 },
        { Packet::dcs, 0x00, parameters2, 0 },
        { Packet::dcs, 0xff, parameters3, 0 },
        { Packet::dcs, 0x00, parameters2, 0 },
        { Packet::dcs, 0xc4, parameters4, 10 },
        { Packet::dcs, 0x00, parameters5, 0 },
        { Packet::dcs, 0xc4, parameters6, 10 },
        { Packet::dcs, 0x00, parameters7, 0 },
        { Packet::dcs, 0xc5, parameters8, 0 },
        { Packet::dcs, 0x00, parameters9, 0 },
        { Packet::dcs, 0xc5, parameters10, 0 },
        { Packet::dcs, 0x00, parameters11, 0 },
        { Packet::dcs, 0xc0, parameters12, 0 },
        { Packet::dcs, 0x00, parameters0, 0 },
        { Packet::dcs, 0xd9, parameters13, 0 },
        { Packet::dcs, 0x00, parameters14, 0 },
        { Packet::dcs, 0xc1, parameters15, 0 },
        { Packet::dcs, 0x00, parameters16, 0 },
        { Packet::dcs, 0xc1, parameters17, 0 },
        { Packet::dcs, 0x00, parameters18, 0 },
        { Packet::dcs, 0xc5, parameters19, 0 },
        { Packet::dcs, 0x00, parameters20, 0 },
        { Packet::dcs, 0xc5, parameters10, 0 },
        { Packet::dcs, 0x00, parameters0, 0 },
        { Packet::dcs, 0xd8, parameters21, 0 },
        { Packet::dcs, 0x00, parameters22, 0 },
        { Packet::dcs, 0xc5, parameters23, 0 },
        { Packet::dcs, 0x00, parameters24, 0 },
        { Packet::dcs, 0xc0, parameters25, 0 },
        { Packet::dcs, 0x00, parameters26, 0 },
        { Packet::dcs, 0xc5, parameters27, 0 },
        { Packet::dcs, 0x00, parameters14, 0 },
        { Packet::dcs, 0xc4, parameters27, 0 },
        { Packet::dcs, 0x00, parameters16, 0 },
        { Packet::dcs, 0xc1, parameters28, 0 },
        { Packet::dcs, 0x00, parameters29, 0 },
        { Packet::dcs, 0xb3, parameters30, 0 },
        { Packet::dcs, 0x00, parameters2, 0 },
        { Packet::dcs, 0xce, parameters31, 0 },
        { Packet::dcs, 0x00, parameters32, 0 },
        { Packet::dcs, 0xce, parameters33, 0 },
        { Packet::dcs, 0x00, parameters34, 0 },
        { Packet::dcs, 0xce, parameters35, 0 },
        { Packet::dcs, 0x00, parameters36, 0 },
        { Packet::dcs, 0xcf, parameters37, 0 },
        { Packet::dcs, 0x00, parameters38, 0 },
        { Packet::dcs, 0xcf, parameters0, 0 },
        { Packet::dcs, 0x00, parameters2, 0 },
        { Packet::dcs, 0xcb, parameters39, 0 },
        { Packet::dcs, 0x00, parameters40, 0 },
        { Packet::dcs, 0xcb, parameters41, 0 },
        { Packet::dcs, 0x00, parameters32, 0 },
        { Packet::dcs, 0xcb, parameters41, 0 },
        { Packet::dcs, 0x00, parameters34, 0 },
        { Packet::dcs, 0xcb, parameters39, 0 },
        { Packet::dcs, 0x00, parameters36, 0 },
        { Packet::dcs, 0xcb, parameters42, 0 },
        { Packet::dcs, 0x00, parameters38, 0 },
        { Packet::dcs, 0xcb, parameters43, 0 },
        { Packet::dcs, 0x00, parameters44, 0 },
        { Packet::dcs, 0xcb, parameters39, 0 },
        { Packet::dcs, 0x00, parameters45, 0 },
        { Packet::dcs, 0xcb, parameters46, 0 },
        { Packet::dcs, 0x00, parameters2, 0 },
        { Packet::dcs, 0xcc, parameters47, 0 },
        { Packet::dcs, 0x00, parameters40, 0 },
        { Packet::dcs, 0xcc, parameters48, 0 },
        { Packet::dcs, 0x00, parameters32, 0 },
        { Packet::dcs, 0xcc, parameters49, 0 },
        { Packet::dcs, 0x00, parameters34, 0 },
        { Packet::dcs, 0xcc, parameters50, 0 },
        { Packet::dcs, 0x00, parameters36, 0 },
        { Packet::dcs, 0xcc, parameters51, 0 },
        { Packet::dcs, 0x00, parameters38, 0 },
        { Packet::dcs, 0xcc, parameters52, 0 },
        { Packet::dcs, 0x00, parameters14, 0 },
        { Packet::dcs, 0xc5, parameters15, 0 },
        { Packet::dcs, 0x00, parameters53, 0 },
        { Packet::dcs, 0xf5, parameters54, 0 },
        { Packet::dcs, 0x00, parameters7, 0 },
        { Packet::dcs, 0xc6, parameters54, 0 },
        { Packet::dcs, 0x00, parameters0, 0 },
        { Packet::dcs, 0xff, parameters55, 0 },
        { Packet::dcs, 0x00, parameters0, 0 },
        { Packet::dcs, 0x00, parameters0, 0 },
        { Packet::dcs, 0xe1, parameters56, 0 },
        { Packet::dcs, 0x00, parameters0, 0 },
        { Packet::dcs, 0xe2, parameters56, 0 },
    } };

    inline constexpr std::array<Command, 6> afterSleepOut{ {
        { Packet::dcs, 0x2a, parameters57, 0 },
        { Packet::dcs, 0x2b, parameters58, 0 },
        { Packet::dcs, 0x51, parameters59, 0 },
        { Packet::dcs, 0x53, parameters60, 0 },
        { Packet::dcs, 0x55, parameters61, 0 },
        { Packet::dcs, 0x5e, parameters62, 0 },
    } };

    inline drivers::MipiDsiPanelCore::Timings MakeTimings()
    {
        drivers::MipiDsiPanelCore::Timings timings;
        timings.resetPulse = std::chrono::milliseconds(20);
        timings.resetRecovery = std::chrono::milliseconds(10);
        return timings;
    }

    inline drivers::MipiDsiPanelCore::Panel MakePanel()
    {
        return { landscape, landscapeAddressMode, { 0, infra::ConstByteRange() }, beforeSleepOut, afterSleepOut, MakeTimings() };
    }
}
