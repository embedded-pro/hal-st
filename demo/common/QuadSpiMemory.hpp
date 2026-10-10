#pragma once

#include "hal/interfaces/QuadSpi.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/Sequencer.hpp"
#include "services/flash/FlashGeometryQuadSfdp.hpp"
#include "services/flash/FlashQuadSpiSingleSpeed.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace main_
{
    // A quad-SPI flash in its power-up mode, so every command runs on one line and carries a 3-byte address: only the first 16 MB are reachable.
    // The check reads the JEDEC identification and the start of the array twice; the test saves the last sector within reach, erases and programs it, and restores it.
    class QuadSpiMemory
    {
    public:
        static constexpr std::size_t checkBytes = 4096;
        static constexpr std::size_t programBytes = 256;
        static constexpr uint32_t reachableBytes = 1u << 24;

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

        void BeginTest();
        void ReportTest();

    private:
        hal::QuadSpi& spi;
        infra::Function<void(bool ok)> onChecked;
        infra::Function<void(bool ok)> onDone;
        services::FlashGeometryQuadSfdp geometry;
        std::optional<services::FlashQuadSpiSingleSpeed> flash;
        infra::Sequencer sequencer;
        std::array<uint8_t, 3> identification{};
        std::array<uint8_t, checkBytes> firstRead{};
        std::array<uint8_t, checkBytes> secondRead{};
        std::array<uint8_t, checkBytes> saved{};
        std::array<uint8_t, programBytes> programData{};
        std::array<uint8_t, programBytes> verifyData{};
        uint32_t testSector{ 0 };
        uint32_t testAddress{ 0 };
        bool erased{ false };
        bool programmed{ false };
        bool restored{ false };
        bool busy{ false };
    };
}
