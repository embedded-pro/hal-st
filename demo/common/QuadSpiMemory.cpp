#include "demo/common/QuadSpiMemory.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <algorithm>

namespace main_
{
    namespace
    {
        constexpr uint8_t commandReadJedecId = 0x9f;
        constexpr uint8_t commandWriteEnable = 0x06;
        constexpr uint8_t commandReadVolatileConfiguration = 0x85;
        constexpr uint8_t commandWriteVolatileConfiguration = 0x81;
        constexpr uint8_t manufacturerMicron = 0x20;
        constexpr uint8_t dummyCyclesShift = 4;
        constexpr uint8_t dummyCyclesMask = 0xf0;
        constexpr uint8_t maxDummyCycles = 14;
        constexpr uint8_t defaultDummyCycles = 15;
        constexpr uint8_t erasedByte = 0xff;
        constexpr uint32_t threeByteAddressRange = 1u << 24;

        hal::QuadSpi::Header CommandHeader(uint8_t command)
        {
            return hal::QuadSpi::Header{ std::make_optional(command), {}, {}, 0 };
        }

        uint8_t DummyCycles(uint8_t volatileConfiguration)
        {
            return static_cast<uint8_t>((volatileConfiguration & dummyCyclesMask) >> dummyCyclesShift);
        }

        bool DummyCyclesAreDefault(uint8_t dummyCycles)
        {
            return dummyCycles == 0 || dummyCycles == defaultDummyCycles;
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
                AlignDummyCycles();
                CheckFirstBlock();
                CheckLastBlock();
            });
    }

    void QuadSpiMemory::ReadIdentification()
    {
        sequencer.Step([this]()
            {
                spi.ReceiveData(CommandHeader(commandReadJedecId), infra::MakeRange(identification), hal::QuadSpi::Lines::SingleSpeed(), [this]()
                    {
                        sequencer.Continue();
                    });
            });
    }

    void QuadSpiMemory::AlignDummyCycles()
    {
        sequencer.If([this]()
            {
                return FlashKeepsDummyCyclesInVolatileConfiguration();
            });
        sequencer.Step([this]()
            {
                ReadVolatileConfiguration();
            });
        sequencer.Execute([this]()
            {
                volatileConfigurationBefore = volatileConfiguration[0];
            });
        sequencer.If([this]()
            {
                return DummyCyclesNeedAlignment();
            });
        sequencer.Step([this]()
            {
                WriteEnable();
            });
        sequencer.Step([this]()
            {
                WriteVolatileConfiguration();
            });
        sequencer.Step([this]()
            {
                ReadVolatileConfiguration();
            });
        sequencer.EndIf();
        sequencer.Execute([this]()
            {
                TraceDummyCycles();
            });
        sequencer.EndIf();
    }

    bool QuadSpiMemory::FlashKeepsDummyCyclesInVolatileConfiguration() const
    {
        return identification[0] == manufacturerMicron && geometry.ReadDummyCycles() != 0 && geometry.ReadDummyCycles() <= maxDummyCycles;
    }

    bool QuadSpiMemory::DummyCyclesNeedAlignment() const
    {
        const uint8_t dummyCycles = DummyCycles(volatileConfiguration[0]);

        return !DummyCyclesAreDefault(dummyCycles) && dummyCycles != geometry.ReadDummyCycles();
    }

    void QuadSpiMemory::ReadVolatileConfiguration()
    {
        spi.ReceiveData(CommandHeader(commandReadVolatileConfiguration), infra::MakeRange(volatileConfiguration), hal::QuadSpi::Lines::SingleSpeed(), [this]()
            {
                sequencer.Continue();
            });
    }

    void QuadSpiMemory::WriteEnable()
    {
        spi.SendData(CommandHeader(commandWriteEnable), {}, hal::QuadSpi::Lines::SingleSpeed(), [this]()
            {
                sequencer.Continue();
            });
    }

    void QuadSpiMemory::WriteVolatileConfiguration()
    {
        volatileConfiguration[0] = static_cast<uint8_t>((volatileConfiguration[0] & ~dummyCyclesMask) | (geometry.ReadDummyCycles() << dummyCyclesShift));

        spi.SendData(CommandHeader(commandWriteVolatileConfiguration), infra::MakeConstRange(volatileConfiguration), hal::QuadSpi::Lines::SingleSpeed(), [this]()
            {
                sequencer.Continue();
            });
    }

    void QuadSpiMemory::TraceDummyCycles()
    {
        const uint8_t dummyCyclesBefore = DummyCycles(volatileConfigurationBefore);
        const uint8_t dummyCyclesAfter = DummyCycles(volatileConfiguration[0]);
        const auto before = static_cast<uint32_t>(volatileConfigurationBefore);
        const auto after = static_cast<uint32_t>(volatileConfiguration[0]);

        if (before == after)
            services::GlobalTracer().Trace() << "QSPI flash dummy cycle field " << static_cast<uint32_t>(dummyCyclesAfter) << (DummyCyclesAreDefault(dummyCyclesAfter) ? " (default)" : "") << ", volatile configuration register " << infra::hex << after;
        else
            services::GlobalTracer().Trace() << "QSPI flash dummy cycle field " << static_cast<uint32_t>(dummyCyclesBefore) << ", set to " << static_cast<uint32_t>(dummyCyclesAfter) << " as the geometry says, volatile configuration register " << infra::hex << before << " now " << after;
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
