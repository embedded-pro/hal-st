#pragma once

#include "hal/interfaces/QuadSpi.hpp"
#include "infra/util/Function.hpp"
#include "services/flash/FlashGeometryQuadSfdp.hpp"
#include "services/flash/FlashQuadSpiGeneric.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace main_
{
    // A twin quad-SPI flash on bank 1: the check reads the JEDEC identification and the start of the array twice, the test erases, programs and verifies the last sector
    class QuadSpiMemory
    {
    public:
        static constexpr std::size_t checkBytes = 4096;
        static constexpr std::size_t programBytes = 256;

        QuadSpiMemory(hal::QuadSpi& spi, const infra::Function<void(bool ok)>& onChecked);
        QuadSpiMemory(const QuadSpiMemory& other) = delete;
        QuadSpiMemory& operator=(const QuadSpiMemory& other) = delete;

        bool Ready() const;
        uint32_t JedecId() const;
        uint32_t Size() const;

        void Check(const infra::Function<void(bool ok)>& onDone);
        void EraseProgramVerify(const infra::Function<void(bool ok)>& onDone);

    private:
        void GeometryReady();
        void ReadIdentification(const infra::Function<void()>& onDone);
        void Evaluate();

    private:
        hal::QuadSpi& spi;
        infra::Function<void(bool ok)> onChecked;
        infra::Function<void(bool ok)> onDone;
        services::FlashGeometryQuadSfdp geometry;
        std::optional<services::FlashQuadSpiGeneric> flash;
        std::array<uint8_t, 3> identification{};
        std::array<uint8_t, checkBytes> firstRead{};
        std::array<uint8_t, checkBytes> secondRead{};
        std::array<uint8_t, programBytes> programData{};
        std::array<uint8_t, programBytes> verifyData{};
    };
}
