#include "validation/firmware/AesGroup.hpp"

#if defined(STM32WB) || defined(STM32WBA)

#include "validation/firmware/Payload.hpp"

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;
        using DataSwapping = hal::detail::DataSwapping;

        constexpr std::size_t blockSize = 16;

        constexpr std::array<HilChoice<DataSwapping>, 4> swappings{ {
            { "none", DataSwapping::disabled },
            { "half", DataSwapping::halfWord },
            { "byte", DataSwapping::byte },
            { "bit", DataSwapping::bit },
        } };

        HilStatus ParseBlocks(infra::BoundedConstString text, infra::ByteRange storage, std::size_t& size)
        {
            if (services::HilArguments::ParseHex(text, storage, size) != HilStatus::done || size == 0 || size % blockSize != 0)
                return HilStatus::usage;

            return HilStatus::done;
        }
    }

    AesCommands::AesCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , commands{ {
              services::HilBind<AesCommands, &AesCommands::Encrypt>("aes.enc", "<key> <data> [swap=none|half|byte|bit]", *this, context.response),
              services::HilBind<AesCommands, &AesCommands::Decrypt>("aes.dec", "<key> <data> [swap=none|half|byte|bit]", *this, context.response),
          } }
    {}

    infra::MemoryRange<const AesCommands::Command> AesCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus AesCommands::Encrypt(const services::HilArguments& arguments)
    {
        return Run(arguments, false);
    }

    HilStatus AesCommands::Decrypt(const services::HilArguments& arguments)
    {
        return Run(arguments, true);
    }

    HilStatus AesCommands::Run(const services::HilArguments& arguments, bool decrypt)
    {
        if (!arguments.Shape(2, 2, { "swap" }))
            return HilStatus::usage;

        std::size_t keySize = 0;
        std::size_t size = 0;
        if (ParseBlocks(arguments.Positional(0), infra::MakeRange(key), keySize) != HilStatus::done || keySize != key.size())
            return HilStatus::usage;

        if (ParseBlocks(arguments.Positional(1), infra::MakeRange(input), size) != HilStatus::done)
            return HilStatus::usage;

        HilStatus status = HilStatus::done;
        hal::SynchronousAes128EcbStm::Config config;
        arguments.Select("swap", config.dataSwapping, swappings, status);
        if (status != HilStatus::done)
            return status;

        auto data = infra::Head(infra::MakeRange(input), size);
        auto result = infra::Head(infra::MakeRange(output), size);

        hal::SynchronousAes128EcbStm aes{ config };
        aes.SetKey(key);

        if (decrypt)
            aes.Decrypt(data, result);
        else
            aes.Encrypt(data, result);

        auto line = context.response.Ok();
        PrintData(line, result, Output::hex);
        return HilStatus::done;
    }
}

#endif
