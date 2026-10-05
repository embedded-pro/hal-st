#include "validation/firmware/Payload.hpp"
#include "infra/util/Crc.hpp"
#include "infra/util/Endian.hpp"
#include <algorithm>
#include <limits>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr std::array<services::HilChoice<Output>, 2> outputChoices{ {
            { "hex", Output::hex },
            { "crc", Output::crc },
        } };

        uint8_t NextPrbs(uint32_t& state)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<uint8_t>(state);
        }
    }

    HilStatus ParsePayload(const services::HilArguments& arguments, std::size_t positional, infra::ByteRange storage, infra::ByteRange& payload)
    {
        auto text = arguments.Positional(positional);
        auto generated = arguments.Has("len");
        auto patterned = arguments.Has("pattern") || arguments.Has("seed");
        payload = infra::Head(storage, 0);

        if (text != "-")
        {
            if (generated || patterned)
                return HilStatus::usage;

            std::size_t size = 0;
            auto status = services::HilArguments::ParseHex(text, storage, size);
            if (status == HilStatus::done)
                payload = infra::Head(storage, size);

            return status;
        }

        if (!generated)
            return patterned ? HilStatus::usage : HilStatus::done;

        auto status = HilStatus::done;
        auto pattern = Pattern::inc;
        uint32_t seed = 0;
        uint32_t length = 0;
        arguments.Select("pattern", pattern, patternChoices, status);
        arguments.Number("seed", seed, 0, std::numeric_limits<uint32_t>::max(), status);
        arguments.Number("len", length, 1, static_cast<uint32_t>(storage.size()), status);

        if (status != HilStatus::done)
            return status;

        payload = infra::Head(storage, length);
        Generate(payload, pattern, seed);
        return HilStatus::done;
    }

    void Generate(infra::ByteRange data, Pattern pattern, uint32_t seed)
    {
        switch (pattern)
        {
            case Pattern::inc:
                for (std::size_t i = 0; i != data.size(); ++i)
                    data[i] = static_cast<uint8_t>(seed + i);
                break;
            case Pattern::constant:
                std::fill(data.begin(), data.end(), static_cast<uint8_t>(seed));
                break;
            case Pattern::prbs:
            {
                uint32_t state = seed != 0 ? seed : 1;
                for (auto& byte : data)
                    byte = NextPrbs(state);
                break;
            }
        }
    }

    uint32_t Crc32(infra::ConstByteRange data)
    {
        infra::Crc32 crc;
        crc.Update(data);
        return crc.Result();
    }

    HilStatus ParseOutput(const services::HilArguments& arguments, Output& output)
    {
        auto status = HilStatus::done;
        output = Output::hex;
        arguments.Select("out", output, outputChoices, status);
        return status;
    }

    HilStatus CheckOutput(std::size_t size, Output output)
    {
        return output == Output::hex && size > maximumHexOutput ? HilStatus::range : HilStatus::done;
    }

    void PrintData(services::HilResponse::Line& line, infra::ConstByteRange data, Output output)
    {
        if (output == Output::hex)
        {
            line << " data=";
            line.Hex(data);
            return;
        }

        infra::BigEndian<uint32_t> crc{ Crc32(data) };
        line << " len=" << static_cast<uint32_t>(data.size()) << " crc=";
        line.Hex(infra::MakeByteRange(crc));
    }
}
