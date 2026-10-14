#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_OPAMP)

namespace hal
{
    class OpampStm
    {
    public:
        struct Config
        {
            enum class Mode : uint8_t
            {
                follower,
                pga,
                standalone
            };

            enum class Input : uint8_t
            {
                vinp0,
                vinp1,
                vinp2,
                dac
            };

            enum class InvertingInput : uint8_t
            {
                vinm0,
                vinm1
            };

            enum class PgaConnection : uint8_t
            {
                none,
                vinm0Filter,
                vinm0Bias
            };

            constexpr Config()
            {}

            Mode mode{ Mode::follower };
            Input input{ Input::vinp0 };
            InvertingInput invertingInput{ InvertingInput::vinm0 };
            PgaConnection pgaConnection{ PgaConnection::none };
            uint8_t gain{ 2 };
            bool internalOutput{ false };
            bool highSpeed{ false };
            bool calibrate{ true };
        };

        static bool ValidGain(uint8_t gain);
        static bool UsesInvertingPin(const Config& config);

        OpampStm(uint8_t oneBasedIndex, GpioPinStm& nonInverting, GpioPinStm& inverting, GpioPinStm& output, const Config& config = Config());
        OpampStm(const OpampStm& other) = delete;
        OpampStm& operator=(const OpampStm& other) = delete;
        ~OpampStm();

        uint32_t TrimmingP() const;
        uint32_t TrimmingN() const;

    private:
        std::optional<AnalogPinStm> nonInverting;
        std::optional<AnalogPinStm> inverting;
        std::optional<AnalogPinStm> output;
        OPAMP_HandleTypeDef handle{};
    };
}

#endif
