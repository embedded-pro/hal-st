#include "examples/stm32f429i_disco/Ili9341Init.hpp"
#include <array>
#include <chrono>

// The commands and parameters are derived from ili9341.c of the board support package of STMicroelectronics
// for the STM32F429I-DISCO, Copyright 2014 STMicroelectronics, BSD 3-Clause
namespace main_
{
    namespace
    {
        inline constexpr std::array<uint8_t, 0> noParameters{};
        inline constexpr std::array<uint8_t, 3> parameters0{ 0xc3, 0x08, 0x50 };
        inline constexpr std::array<uint8_t, 3> parameters1{ 0x00, 0xc1, 0x30 };
        inline constexpr std::array<uint8_t, 4> parameters2{ 0x64, 0x03, 0x12, 0x81 };
        inline constexpr std::array<uint8_t, 3> parameters3{ 0x85, 0x00, 0x78 };
        inline constexpr std::array<uint8_t, 5> parameters4{ 0x39, 0x2c, 0x00, 0x34, 0x02 };
        inline constexpr std::array<uint8_t, 1> parameters5{ 0x20 };
        inline constexpr std::array<uint8_t, 2> parameters6{ 0x00, 0x00 };
        inline constexpr std::array<uint8_t, 2> parameters7{ 0x00, 0x1b };
        inline constexpr std::array<uint8_t, 2> parameters8{ 0x0a, 0xa2 };
        inline constexpr std::array<uint8_t, 1> parameters9{ 0x10 };
        inline constexpr std::array<uint8_t, 2> parameters10{ 0x45, 0x15 };
        inline constexpr std::array<uint8_t, 1> parameters11{ 0x90 };
        inline constexpr std::array<uint8_t, 1> parameters12{ 0xc8 };
        inline constexpr std::array<uint8_t, 1> parameters13{ 0x00 };
        inline constexpr std::array<uint8_t, 1> parameters14{ 0xc2 };
        inline constexpr std::array<uint8_t, 4> parameters15{ 0x0a, 0xa7, 0x27, 0x04 };
        inline constexpr std::array<uint8_t, 4> parameters16{ 0x00, 0x00, 0x00, 0xef };
        inline constexpr std::array<uint8_t, 4> parameters17{ 0x00, 0x00, 0x01, 0x3f };
        inline constexpr std::array<uint8_t, 3> parameters18{ 0x01, 0x00, 0x06 };
        inline constexpr std::array<uint8_t, 1> parameters19{ 0x01 };
        inline constexpr std::array<uint8_t, 15> parameters20{ 0x0f, 0x29, 0x24, 0x0c, 0x0e, 0x09, 0x4e, 0x78, 0x3c, 0x09, 0x13, 0x05, 0x17, 0x11, 0x00 };
        inline constexpr std::array<uint8_t, 15> parameters21{ 0x00, 0x16, 0x1b, 0x04, 0x11, 0x07, 0x31, 0x33, 0x42, 0x05, 0x0c, 0x0a, 0x28, 0x2f, 0x0f };

        inline constexpr std::array<Ili9341Init::Command, 27> commands{ {
            { 0xca, parameters0, 0 },
            { 0xcf, parameters1, 0 },
            { 0xed, parameters2, 0 },
            { 0xe8, parameters3, 0 },
            { 0xcb, parameters4, 0 },
            { 0xf7, parameters5, 0 },
            { 0xea, parameters6, 0 },
            { 0xb1, parameters7, 0 },
            { 0xb6, parameters8, 0 },
            { 0xc0, parameters9, 0 },
            { 0xc1, parameters9, 0 },
            { 0xc5, parameters10, 0 },
            { 0xc7, parameters11, 0 },
            { 0x36, parameters12, 0 },
            { 0xf2, parameters13, 0 },
            { 0xb0, parameters14, 0 },
            { 0xb6, parameters15, 0 },
            { 0x2a, parameters16, 0 },
            { 0x2b, parameters17, 0 },
            { 0xf6, parameters18, 0 },
            { 0x2c, noParameters, 200 },
            { 0x26, parameters19, 0 },
            { 0xe0, parameters20, 0 },
            { 0xe1, parameters21, 0 },
            { 0x11, noParameters, 200 },
            { 0x29, noParameters, 0 },
            { 0x2c, noParameters, 0 },
        } };
    }

    Ili9341Init::Ili9341Init(hal::SpiMaster& spi, hal::GpioPin& chipSelect, hal::GpioPin& dataCommand, const infra::Function<void()>& onInitialized)
        : spi(spi)
        , chipSelect(chipSelect, true)
        , dataCommand(dataCommand)
        , onInitialized(onInitialized)
    {
        spi.SetChipSelectConfigurator(*this);
        SendCommand();
    }

    void Ili9341Init::StartSession()
    {
        chipSelect.Set(false);
    }

    void Ili9341Init::EndSession()
    {
        chipSelect.Set(true);
    }

    void Ili9341Init::SendCommand()
    {
        if (index == commands.size())
        {
            onInitialized();
            return;
        }

        const Command& command = commands[index];
        commandByte = command.command;
        dataCommand.Set(false);

        spi.SendData(infra::MakeByteRange(commandByte), command.parameters.empty() ? hal::SpiAction::stop : hal::SpiAction::continueSession, [this]()
            {
                CommandByteSent();
            });
    }

    void Ili9341Init::CommandByteSent()
    {
        const Command& command = commands[index];

        if (command.parameters.empty())
            CommandSent();
        else
        {
            dataCommand.Set(true);
            spi.SendData(command.parameters, hal::SpiAction::stop, [this]()
                {
                    CommandSent();
                });
        }
    }

    void Ili9341Init::CommandSent()
    {
        uint16_t delay = commands[index].delayAfterInMilliseconds;
        ++index;

        if (delay != 0)
            timer.Start(std::chrono::milliseconds(delay), [this]()
                {
                    SendCommand();
                });
        else
            SendCommand();
    }
}
