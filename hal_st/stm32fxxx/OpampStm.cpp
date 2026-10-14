#include "hal_st/stm32fxxx/OpampStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>

#if defined(HAS_PERIPHERAL_OPAMP)

#if defined(STM32G4)
#include "stm32g4xx_ll_opamp.h"
#endif

namespace hal
{
    namespace
    {
        constexpr std::array<uint32_t, 3> modes{ { OPAMP_FOLLOWER_MODE, OPAMP_PGA_MODE, OPAMP_STANDALONE_MODE } };

        constexpr std::array<uint32_t, 4> inputs{ {
            OPAMP_NONINVERTINGINPUT_IO0,
            OPAMP_NONINVERTINGINPUT_IO1,
            OPAMP_NONINVERTINGINPUT_IO2,
            OPAMP_NONINVERTINGINPUT_DAC,
        } };

        constexpr std::array<uint32_t, 3> pgaConnections{ {
            OPAMP_PGA_CONNECT_INVERTINGINPUT_NO,
            OPAMP_PGA_CONNECT_INVERTINGINPUT_IO0,
            OPAMP_PGA_CONNECT_INVERTINGINPUT_IO0_BIAS,
        } };

        constexpr std::array<uint32_t, 6> gains{ {
            OPAMP_PGA_GAIN_2_OR_MINUS_1,
            OPAMP_PGA_GAIN_4_OR_MINUS_3,
            OPAMP_PGA_GAIN_8_OR_MINUS_7,
            OPAMP_PGA_GAIN_16_OR_MINUS_15,
            OPAMP_PGA_GAIN_32_OR_MINUS_31,
            OPAMP_PGA_GAIN_64_OR_MINUS_63,
        } };

        std::size_t GainIndex(uint8_t gain)
        {
            std::size_t index = 0;
            while ((2u << index) < gain)
                ++index;

            return index;
        }
    }

    bool OpampStm::ValidGain(uint8_t gain)
    {
        return gain >= 2 && gain <= 64 && (gain & (gain - 1)) == 0;
    }

    bool OpampStm::UsesInvertingPin(const Config& config)
    {
        return config.mode == Config::Mode::standalone || (config.mode == Config::Mode::pga && config.pgaConnection != Config::PgaConnection::none);
    }

    OpampStm::OpampStm(uint8_t oneBasedIndex, GpioPinStm& nonInverting, GpioPinStm& inverting, GpioPinStm& output, const Config& config)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralOpamp.size());
        really_assert(config.mode != Config::Mode::pga || ValidGain(config.gain));

        if (config.input != Config::Input::dac)
            this->nonInverting.emplace(nonInverting);
        if (UsesInvertingPin(config))
            this->inverting.emplace(inverting);
        if (!config.internalOutput)
            this->output.emplace(output);

        EnableClockOpamp(oneBasedIndex - 1);
        __HAL_RCC_SYSCFG_CLK_ENABLE();

        handle.Instance = peripheralOpamp[oneBasedIndex - 1];
        handle.Init.PowerMode = config.highSpeed ? OPAMP_POWERMODE_HIGHSPEED : OPAMP_POWERMODE_NORMALSPEED;
        handle.Init.Mode = modes[static_cast<std::size_t>(config.mode)];
        handle.Init.InvertingInput = config.invertingInput == Config::InvertingInput::vinm1 ? OPAMP_INVERTINGINPUT_IO1 : OPAMP_INVERTINGINPUT_IO0;
        handle.Init.NonInvertingInput = inputs[static_cast<std::size_t>(config.input)];
        handle.Init.InternalOutput = config.internalOutput ? ENABLE : DISABLE;
        handle.Init.TimerControlledMuxmode = OPAMP_TIMERCONTROLLEDMUXMODE_DISABLE;
        handle.Init.PgaConnect = pgaConnections[static_cast<std::size_t>(config.pgaConnection)];
        handle.Init.PgaGain = gains[GainIndex(config.mode == Config::Mode::pga ? config.gain : 2)];
        handle.Init.UserTrimming = OPAMP_TRIMMING_FACTORY;
        really_assert(HAL_OPAMP_Init(&handle) == HAL_OK);

        if (config.calibrate)
            really_assert(HAL_OPAMP_SelfCalibrate(&handle) == HAL_OK);

        really_assert(HAL_OPAMP_Start(&handle) == HAL_OK);
    }

    OpampStm::~OpampStm()
    {
        HAL_OPAMP_Stop(&handle);
        HAL_OPAMP_DeInit(&handle);
    }

    uint32_t OpampStm::TrimmingP() const
    {
        return LL_OPAMP_GetTrimmingValue(handle.Instance, LL_OPAMP_TRIMMING_PMOS);
    }

    uint32_t OpampStm::TrimmingN() const
    {
        return LL_OPAMP_GetTrimmingValue(handle.Instance, LL_OPAMP_TRIMMING_NMOS);
    }
}

#endif
