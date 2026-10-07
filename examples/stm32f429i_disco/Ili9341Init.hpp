#pragma once

#include "hal/interfaces/Gpio.hpp"
#include "hal/interfaces/Spi.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>

namespace main_
{
    // Sets up the ILI9341 of the STM32F429I-DISCO to take its pixels from the RGB interface of the LTDC.
    // The commands and their parameters are those of the board support package of STMicroelectronics.
    // The chip select is a plain pin because the panel is not on the slave select pin of the SPI
    class Ili9341Init
        : private hal::ChipSelectConfigurator
    {
    public:
        struct Command
        {
            uint8_t command;
            infra::ConstByteRange parameters;
            uint16_t delayAfterInMilliseconds;
        };

        Ili9341Init(hal::SpiMaster& spi, hal::GpioPin& chipSelect, hal::GpioPin& dataCommand, const infra::Function<void()>& onInitialized);

    private:
        void StartSession() override;
        void EndSession() override;

        void SendCommand();
        void CommandByteSent();
        void CommandSent();

    private:
        hal::SpiMaster& spi;
        hal::OutputPin chipSelect;
        hal::OutputPin dataCommand;
        infra::Function<void()> onInitialized;
        infra::TimerSingleShot timer;
        std::size_t index{ 0 };
        uint8_t commandByte{ 0 };
    };
}
