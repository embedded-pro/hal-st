#include "validation/firmware/BackupRamGroup.hpp"
#include "hal_st/stm32fxxx/BackupRamStm.hpp"
#include "infra/util/Endian.hpp"
#include <limits>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint32_t fillStep = 0x9e3779b9;

        hal::BackupRam<volatile uint32_t>& Instance()
        {
            static hal::BackupRamStm backupRam;
            return backupRam;
        }

        uint32_t FillValue(uint32_t seed, uint32_t index)
        {
            return seed ^ (fillStep * (index + 1));
        }

        void PrintValue(services::HilResponse::Line& line, uint32_t value)
        {
            infra::BigEndian<uint32_t> bytes{ value };
            line << " value=";
            line.Hex(infra::MakeByteRange(bytes));
        }
    }

    BackupRamCommands::BackupRamCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , backupRam(Instance())
        , commands{ {
              services::HilBind<BackupRamCommands, &BackupRamCommands::Info>("bkp.info", "", *this, context.response),
              services::HilBind<BackupRamCommands, &BackupRamCommands::Write>("bkp.write", "<index> <value>", *this, context.response),
              services::HilBind<BackupRamCommands, &BackupRamCommands::Read>("bkp.read", "<index>", *this, context.response),
              services::HilBind<BackupRamCommands, &BackupRamCommands::Fill>("bkp.fill", "<seed>", *this, context.response),
              services::HilBind<BackupRamCommands, &BackupRamCommands::Check>("bkp.check", "<seed>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const BackupRamCommands::Command> BackupRamCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus BackupRamCommands::Info(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        context.response.Ok() << " words=" << static_cast<uint32_t>(backupRam.Get().size());
        return HilStatus::done;
    }

    HilStatus BackupRamCommands::Write(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(2, 2, {}))
            return HilStatus::usage;

        uint32_t index = 0;
        uint32_t value = 0;
        auto status = ParseIndex(arguments, index);
        arguments.NumberAt(1, value, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status != HilStatus::done)
            return status;

        backupRam.Get()[index] = value;
        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus BackupRamCommands::Read(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        uint32_t index = 0;
        auto status = ParseIndex(arguments, index);
        if (status != HilStatus::done)
            return status;

        auto line = context.response.Ok();
        PrintValue(line, backupRam.Get()[index]);
        return HilStatus::done;
    }

    HilStatus BackupRamCommands::Fill(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        uint32_t seed = 0;
        auto status = HilStatus::done;
        arguments.NumberAt(0, seed, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status != HilStatus::done)
            return status;

        auto words = backupRam.Get();
        for (uint32_t index = 0; index != words.size(); ++index)
            words[index] = FillValue(seed, index);

        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus BackupRamCommands::Check(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, {}))
            return HilStatus::usage;

        uint32_t seed = 0;
        auto status = HilStatus::done;
        arguments.NumberAt(0, seed, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status != HilStatus::done)
            return status;

        auto words = backupRam.Get();
        uint32_t mismatches = 0;
        for (uint32_t index = 0; index != words.size(); ++index)
            if (words[index] != FillValue(seed, index))
                ++mismatches;

        context.response.Ok() << " mismatches=" << mismatches;
        return HilStatus::done;
    }

    HilStatus BackupRamCommands::ParseIndex(const services::HilArguments& arguments, uint32_t& index) const
    {
        auto status = HilStatus::done;
        arguments.NumberAt(0, index, 0, static_cast<uint32_t>(backupRam.Get().size()) - 1, status);
        return status;
    }
}
