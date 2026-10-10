#pragma once

#include "hal/interfaces/QuadSpi.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/Sequencer.hpp"
#include "services/flash/FlashGeometryQuadSfdp.hpp"
#include "services/flash/FlashQuadSpiGeneric.hpp"
#include "services/flash/FlashQuadSpiSingleSpeed.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace main_
{
    // A quad-SPI flash in its power-up mode. Reads and programs use the quad lines, and every read is compared with a read on one line.
    // The check reads the JEDEC identification and the first and the last block of the array; the test saves the last sector, erases and programs it, and restores it.
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
        using Buffer = std::array<uint8_t, checkBytes>;

        void GeometryReady();
        bool Begin(const infra::Function<void(bool ok)>& onDone);
        void Finish(bool ok);

        void BeginCheck();
        void ReadIdentification();
        void CheckFirstBlock();
        void CheckLastBlock();
        void EvaluateCheck();

        void BeginTest();
        void SaveTestSector();
        void ProgramTestSector();
        void VerifyTestSector();
        void RestoreTestSector();
        void ReportTest();
        bool AliasIsSeparate() const;

        void ReadSingleLine(Buffer& buffer, uint32_t address);
        void ReadQuad(Buffer& buffer, uint32_t address);
        void ProgramQuad(infra::ConstByteRange data, uint32_t address);
        void EraseTestSector();

    private:
        hal::QuadSpi& spi;
        infra::Function<void(bool ok)> onChecked;
        infra::Function<void(bool ok)> onDone;
        services::FlashGeometryQuadSfdp geometry;
        std::optional<services::FlashQuadSpiGeneric> quadFlash;
        std::optional<services::FlashQuadSpiSingleSpeed> singleLineFlash;
        infra::Sequencer sequencer;
        std::array<uint8_t, 3> identification{};
        Buffer reference{};
        Buffer candidate{};
        Buffer saved{};
        Buffer alias{};
        std::array<uint8_t, programBytes> programData{};
        uint32_t lastBlockAddress{ 0 };
        uint32_t firstWord{ 0 };
        std::size_t blank{ 0 };
        bool stable{ false };
        bool quadFirstBlockMatches{ false };
        bool quadLastBlockMatches{ false };
        uint32_t testSector{ 0 };
        uint32_t testAddress{ 0 };
        uint32_t aliasAddress{ 0 };
        bool erased{ false };
        bool programmedOnFourLines{ false };
        bool programmedReadOnOneLine{ false };
        bool aliasUntouched{ false };
        bool restored{ false };
        bool busy{ false };
    };
}
