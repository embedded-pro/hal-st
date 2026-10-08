#pragma once

#include <array>
#include <cstdint>
#include DEVICE_HEADER

namespace hal
{
    class AudioClockSwitchStm
    {
    public:
        AudioClockSwitchStm(const RCC_PeriphCLKInitTypeDef& clock48kFamily, const RCC_PeriphCLKInitTypeDef& clock44k1Family);
        AudioClockSwitchStm(const AudioClockSwitchStm& other) = delete;
        AudioClockSwitchStm& operator=(const AudioClockSwitchStm& other) = delete;

        static bool Is48kFamily(uint32_t sampleRate);
        static bool Is44k1Family(uint32_t sampleRate);

        void Select(uint32_t sampleRate);

    private:
        enum class Family : uint8_t
        {
            none,
            rate48k,
            rate44k1
        };

    private:
        std::array<RCC_PeriphCLKInitTypeDef, 2> clocks;
        Family active{ Family::none };
    };
}
