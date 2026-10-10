#include "demo/common/QuadSpiMemory.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <algorithm>

namespace main_
{
    namespace
    {
        constexpr uint8_t commandReadJedecId = 0x9f;
        constexpr uint8_t erasedByte = 0xff;
        constexpr uint32_t threeByteAddressRange = 1u << 24;

        hal::QuadSpi::Header JedecHeader()
        {
            return hal::QuadSpi::Header{ std::make_optional<uint8_t>(commandReadJedecId), {}, {}, 0 };
        }

        bool IsErased(infra::ConstByteRange data)
        {
            return std::all_of(data.begin(), data.end(), [](uint8_t value)
                {
                    return value == erasedByte;
                });
        }

        uint32_t BigEndianWord(const uint8_t* bytes)
        {
            return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 | static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
        }

        const char* Result(bool ok)
        {
            return ok ? "ok" : "failed";
        }
    }

    QuadSpiMemory::QuadSpiMemory(hal::QuadSpi& spi, const infra::Function<void(bool ok)>& onChecked)
        : spi(spi)
        , onChecked(onChecked)
        , geometry(spi, [this]()
              {
                  GeometryReady();
              })
    {}

    bool QuadSpiMemory::Ready() const
    {
        return quadFlash.has_value();
    }

    uint32_t QuadSpiMemory::JedecId() const
    {
        return static_cast<uint32_t>(identification[0] << 16 | identification[1] << 8 | identification[2]);
    }

    uint32_t QuadSpiMemory::Size() const
    {
        return quadFlash ? quadFlash->TotalSize() : 0;
    }

    void QuadSpiMemory::Check(const infra::Function<void(bool ok)>& onDone)
    {
        if (Begin(onDone))
            BeginCheck();
    }

    void QuadSpiMemory::EraseProgramVerify(const infra::Function<void(bool ok)>& onDone)
    {
        if (Begin(onDone))
            BeginTest();
    }

    void QuadSpiMemory::GeometryReady()
    {
        quadFlash.emplace(spi, geometry, services::FlashQuadSpiGeneric::Protocol::extendedSpi);
        singleLineFlash.emplace(spi, geometry, infra::emptyFunction);

        Check([this](bool ok)
            {
                onChecked(ok);
            });
    }

    bool QuadSpiMemory::Begin(const infra::Function<void(bool ok)>& onDone)
    {
        if (!quadFlash)
        {
            onDone(false);
            return false;
        }

        if (busy)
        {
            services::GlobalTracer().Trace() << "QSPI busy";
            return false;
        }

        busy = true;
        this->onDone = onDone;
        return true;
    }

    void QuadSpiMemory::Finish(bool ok)
    {
        busy = false;

        auto done = onDone;
        done(ok);
    }

    void QuadSpiMemory::ReadSingleLine(Buffer& buffer, uint32_t address)
    {
        singleLineFlash->ReadBuffer(infra::MakeRange(buffer), address, [this]()
            {
                sequencer.Continue();
            });
    }

    void QuadSpiMemory::ReadQuad(Buffer& buffer, uint32_t address)
    {
        quadFlash->ReadBuffer(infra::MakeRange(buffer), address, [this]()
            {
                sequencer.Continue();
            });
    }

    void QuadSpiMemory::ProgramQuad(infra::ConstByteRange data, uint32_t address)
    {
        quadFlash->WriteBuffer(data, address, [this]()
            {
                sequencer.Continue();
            });
    }

    void QuadSpiMemory::EraseTestSector()
    {
        quadFlash->EraseSector(testSector, [this]()
            {
                sequencer.Continue();
            });
    }

    void QuadSpiMemory::BeginCheck()
    {
        lastBlockAddress = Size() - checkBytes;

        sequencer.Load([this]()
            {
                ReadIdentification();
                CheckFirstBlock();
                CheckLastBlock();
            });
    }

    void QuadSpiMemory::ReadIdentification()
    {
        sequencer.Step([this]()
            {
                spi.ReceiveData(JedecHeader(), infra::MakeRange(identification), hal::QuadSpi::Lines::SingleSpeed(), [this]()
                    {
                        sequencer.Continue();
                    });
            });
    }

    void QuadSpiMemory::CheckFirstBlock()
    {
        sequencer.Step([this]()
            {
                ReadSingleLine(reference, 0);
            });
        sequencer.Execute([this]()
            {
                blank = static_cast<std::size_t>(std::count(reference.begin(), reference.end(), erasedByte));
                firstWord = BigEndianWord(reference.data());
            });
        sequencer.Step([this]()
            {
                ReadSingleLine(candidate, 0);
            });
        sequencer.Execute([this]()
            {
                stable = reference == candidate;
            });
        sequencer.Step([this]()
            {
                ReadQuad(candidate, 0);
            });
        sequencer.Execute([this]()
            {
                quadFirstBlockMatches = reference == candidate;
            });
    }

    void QuadSpiMemory::CheckLastBlock()
    {
        sequencer.Step([this]()
            {
                ReadSingleLine(reference, lastBlockAddress);
            });
        sequencer.Step([this]()
            {
                ReadQuad(candidate, lastBlockAddress);
            });
        sequencer.Execute([this]()
            {
                quadLastBlockMatches = reference == candidate;
                EvaluateCheck();
            });
    }

    void QuadSpiMemory::EvaluateCheck()
    {
        const bool identified = identification[0] != 0x00 && identification[0] != erasedByte;

        services::GlobalTracer().Trace() << "QSPI " << Size() / (1024 * 1024) << " MB, first " << static_cast<uint32_t>(checkBytes) << " bytes read twice: " << (stable ? "stable" : "unstable") << ", " << static_cast<uint32_t>(blank) << " blank, first word " << infra::hex << firstWord << ", JEDEC " << JedecId();
        services::GlobalTracer().Trace() << "QSPI read on four lines matches the read on one line: first block " << Result(quadFirstBlockMatches) << ", last block at " << infra::hex << lastBlockAddress << " " << Result(quadLastBlockMatches);

        Finish(identified && stable && quadFirstBlockMatches && quadLastBlockMatches);
    }

    void QuadSpiMemory::BeginTest()
    {
        testSector = quadFlash->NumberOfSectors() - 1;
        testAddress = quadFlash->AddressOfSector(testSector);
        aliasAddress = testAddress % threeByteAddressRange;
        erased = false;
        programmedOnFourLines = false;
        programmedReadOnOneLine = false;
        aliasUntouched = false;
        restored = false;

        for (std::size_t index = 0; index != programData.size(); ++index)
            programData[index] = static_cast<uint8_t>(index * 7 + 1);

        sequencer.Load([this]()
            {
                SaveTestSector();
                ProgramTestSector();
                VerifyTestSector();
                RestoreTestSector();
            });
    }

    void QuadSpiMemory::SaveTestSector()
    {
        sequencer.Step([this]()
            {
                ReadSingleLine(saved, testAddress);
            });
        sequencer.Step([this]()
            {
                ReadSingleLine(alias, aliasAddress);
            });
    }

    void QuadSpiMemory::ProgramTestSector()
    {
        sequencer.Step([this]()
            {
                EraseTestSector();
            });
        sequencer.Step([this]()
            {
                ReadQuad(candidate, testAddress);
            });
        sequencer.Execute([this]()
            {
                erased = IsErased(infra::MakeConstRange(candidate));
            });
        sequencer.Step([this]()
            {
                ProgramQuad(infra::MakeConstRange(programData), testAddress);
            });
    }

    void QuadSpiMemory::VerifyTestSector()
    {
        sequencer.Step([this]()
            {
                ReadQuad(candidate, testAddress);
            });
        sequencer.Execute([this]()
            {
                programmedOnFourLines = std::equal(programData.begin(), programData.end(), candidate.begin());
            });
        sequencer.Step([this]()
            {
                ReadSingleLine(reference, testAddress);
            });
        sequencer.Execute([this]()
            {
                programmedReadOnOneLine = std::equal(programData.begin(), programData.end(), reference.begin());
            });
        sequencer.Step([this]()
            {
                ReadSingleLine(candidate, aliasAddress);
            });
        sequencer.Execute([this]()
            {
                aliasUntouched = !AliasIsSeparate() || candidate == alias;
            });
    }

    void QuadSpiMemory::RestoreTestSector()
    {
        sequencer.Step([this]()
            {
                EraseTestSector();
            });
        sequencer.Step([this]()
            {
                ProgramQuad(infra::MakeConstRange(saved), testAddress);
            });
        sequencer.Step([this]()
            {
                ReadSingleLine(candidate, testAddress);
            });
        sequencer.Execute([this]()
            {
                restored = candidate == saved;
                ReportTest();
            });
    }

    bool QuadSpiMemory::AliasIsSeparate() const
    {
        return aliasAddress != testAddress;
    }

    void QuadSpiMemory::ReportTest()
    {
        services::GlobalTracer().Trace() << "QSPI test of the sector at " << infra::hex << testAddress << ": erase " << Result(erased) << ", program on four lines " << Result(programmedOnFourLines) << ", read on one line " << Result(programmedReadOnOneLine) << ", restore " << Result(restored);

        if (AliasIsSeparate())
            services::GlobalTracer().Trace() << "QSPI sector at " << infra::hex << aliasAddress << ", where a 3-byte address would land: untouched " << Result(aliasUntouched);

        Finish(erased && programmedOnFourLines && programmedReadOnOneLine && aliasUntouched && restored);
    }
}
