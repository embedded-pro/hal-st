#include "hal_st/stm32fxxx/AudioClockSwitchStm.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace hal
{
    namespace
    {
        constexpr uint32_t base48k = 8000;
        constexpr uint32_t top48k = 96000;
        constexpr uint32_t base44k1 = 11025;
    }

    AudioClockSwitchStm::AudioClockSwitchStm(const RCC_PeriphCLKInitTypeDef& clock48kFamily, const RCC_PeriphCLKInitTypeDef& clock44k1Family)
        : clocks{ { clock48kFamily, clock44k1Family } }
    {}

    bool AudioClockSwitchStm::Is48kFamily(uint32_t sampleRate)
    {
        return sampleRate != 0 && (sampleRate % base48k == 0 || top48k % sampleRate == 0);
    }

    bool AudioClockSwitchStm::Is44k1Family(uint32_t sampleRate)
    {
        return sampleRate != 0 && sampleRate % base44k1 == 0;
    }

    void AudioClockSwitchStm::Select(uint32_t sampleRate)
    {
        const Family family = Is48kFamily(sampleRate) ? Family::rate48k : (Is44k1Family(sampleRate) ? Family::rate44k1 : Family::none);
        really_assert(family != Family::none);

        if (family == active)
            return;

        RCC_PeriphCLKInitTypeDef clock = clocks[family == Family::rate48k ? 0 : 1];
        const HAL_StatusTypeDef status = HAL_RCCEx_PeriphCLKConfig(&clock);
        really_assert(status == HAL_OK);

        active = family;
    }
}
