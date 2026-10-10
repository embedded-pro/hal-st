#include "validation/firmware/BoardInfoStm.hpp"
#include "BoardProfile.hpp"
#include "hal_st/stm32fxxx/UniqueDeviceId.hpp"
#include "services/hil/HilArguments.hpp"
#include <array>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        // A reset through NRST also sets PINRSTF, so the pin is only the cause when nothing else is flagged
#if defined(STM32G4)
        constexpr std::array<services::HilChoice<uint32_t>, 7> resetCauses{ {
            { "iwdg", RCC_FLAG_IWDGRST },
            { "wwdg", RCC_FLAG_WWDGRST },
            { "sw", RCC_FLAG_SFTRST },
            { "lpwr", RCC_FLAG_LPWRRST },
            { "obl", RCC_FLAG_OBLRST },
            { "bor", RCC_FLAG_BORRST },
            { "pin", RCC_FLAG_PINRST },
        } };
#else
        constexpr std::array<services::HilChoice<uint32_t>, 7> resetCauses{ {
            { "iwdg", RCC_RESET_FLAG_IWDG },
            { "wwdg", RCC_RESET_FLAG_WWDG },
            { "sw", RCC_RESET_FLAG_SW },
            { "lpwr", RCC_RESET_FLAG_LPWR },
            { "obl", RCC_RESET_FLAG_OBL },
            { "bor", RCC_RESET_FLAG_PWR },
            { "pin", RCC_RESET_FLAG_PIN },
        } };
#endif
    }

    const char* ReadAndClearResetCause()
    {
#if defined(STM32G4)
        const char* cause = "unknown";

        for (const auto& entry : resetCauses)
            if (__HAL_RCC_GET_FLAG(entry.value) != 0)
            {
                cause = entry.name;
                break;
            }

        __HAL_RCC_CLEAR_RESET_FLAGS();
        return cause;
#else
        const uint32_t cause = HAL_RCC_GetResetSource();

        for (const auto& entry : resetCauses)
            if ((cause & entry.value) != 0)
                return entry.name;

        return "unknown";
#endif
    }

    BoardInfoStm::BoardInfoStm(const char* resetCause)
        : resetCause(resetCause)
    {}

    const char* BoardInfoStm::Name() const
    {
        return board::name;
    }

    const char* BoardInfoStm::Family() const
    {
        return board::family;
    }

    uint32_t BoardInfoStm::SystemClock() const
    {
        return SystemCoreClock;
    }

    const char* BoardInfoStm::ResetCause() const
    {
        return resetCause;
    }

    infra::ConstByteRange BoardInfoStm::UniqueId() const
    {
        return hal::UniqueDeviceId();
    }
}
