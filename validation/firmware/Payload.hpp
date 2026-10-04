#pragma once

#include "infra/util/ByteRange.hpp"
#include "services/hil/HilArguments.hpp"
#include "services/hil/HilResponse.hpp"
#include "services/hil/HilStatus.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    enum class Pattern : uint8_t
    {
        inc,
        constant,
        prbs,
    };

    enum class Output : uint8_t
    {
        hex,
        crc,
    };

    inline constexpr std::array<const char*, 3> payloadKeys{ { "len", "pattern", "seed" } };
    inline constexpr std::array<services::HilChoice<Pattern>, 3> patternChoices{ {
        { "inc", Pattern::inc },
        { "const", Pattern::constant },
        { "prbs", Pattern::prbs },
    } };
    inline constexpr std::size_t maximumHexOutput = 128;

    services::HilStatus ParsePayload(const services::HilArguments& arguments, std::size_t positional, infra::ByteRange storage, infra::ByteRange& payload);
    void Generate(infra::ByteRange data, Pattern pattern, uint32_t seed);
    uint32_t Crc32(infra::ConstByteRange data);

    services::HilStatus ParseOutput(const services::HilArguments& arguments, Output& output);
    services::HilStatus CheckOutput(std::size_t size, Output output);
    void PrintData(services::HilResponse::Line& line, infra::ConstByteRange data, Output output);
}
