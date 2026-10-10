#include "demo/common/QuadSpiMemory.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <algorithm>

namespace main_
{
    namespace
    {
        constexpr uint8_t commandReadJedecId = 0x9f;
        constexpr uint8_t erasedByte = 0xff;

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
        return flash.has_value();
    }

    uint32_t QuadSpiMemory::JedecId() const
    {
        return static_cast<uint32_t>(identification[0] << 16 | identification[1] << 8 | identification[2]);
    }

    uint32_t QuadSpiMemory::Size() const
    {
        return flash ? flash->TotalSize() : 0;
    }

    void QuadSpiMemory::Check(const infra::Function<void(bool ok)>& onDone)
    {
        if (!flash)
        {
            onDone(false);
            return;
        }

        if (busy)
        {
            services::GlobalTracer().Trace() << "QSPI busy";
            return;
        }

        busy = true;
        this->onDone = onDone;

        ReadIdentification([this]()
            {
                flash->ReadBuffer(infra::MakeRange(firstRead), 0, [this]()
                    {
                        flash->ReadBuffer(infra::MakeRange(secondRead), 0, [this]()
                            {
                                Evaluate();
                            });
                    });
            });
    }

    void QuadSpiMemory::EraseProgramVerify(const infra::Function<void(bool ok)>& onDone)
    {
        if (!flash)
        {
            onDone(false);
            return;
        }

        if (busy)
        {
            services::GlobalTracer().Trace() << "QSPI busy";
            return;
        }

        busy = true;
        this->onDone = onDone;

        BeginTest();
    }

    void QuadSpiMemory::GeometryReady()
    {
        flash.emplace(spi, geometry, infra::emptyFunction);

        Check([this](bool ok)
            {
                onChecked(ok);
            });
    }

    void QuadSpiMemory::ReadIdentification(const infra::Function<void()>& onDone)
    {
        spi.ReceiveData(JedecHeader(), infra::MakeRange(identification), hal::QuadSpi::Lines::SingleSpeed(), onDone);
    }

    void QuadSpiMemory::Evaluate()
    {
        const bool stable = firstRead == secondRead;
        const std::size_t blank = static_cast<std::size_t>(std::count(firstRead.begin(), firstRead.end(), erasedByte));
        const bool identified = identification[0] != 0x00 && identification[0] != erasedByte;
        const uint32_t firstWord = static_cast<uint32_t>(firstRead[0] << 24 | firstRead[1] << 16 | firstRead[2] << 8 | firstRead[3]);

        services::GlobalTracer().Trace() << "QSPI " << Size() / (1024 * 1024) << " MB, first " << static_cast<uint32_t>(checkBytes) << " bytes read twice: " << (stable ? "stable" : "unstable") << ", " << static_cast<uint32_t>(blank) << " blank, first word " << infra::hex << firstWord << ", JEDEC " << JedecId();

        busy = false;
        onDone(identified && stable);
    }

    void QuadSpiMemory::BeginTest()
    {
        const uint32_t reachableSectors = reachableBytes / flash->SizeOfSector(0);
        testSector = std::min(flash->NumberOfSectors(), reachableSectors) - 1;
        testAddress = flash->AddressOfSector(testSector);
        erased = false;
        programmed = false;
        restored = false;

        for (std::size_t index = 0; index != programData.size(); ++index)
            programData[index] = static_cast<uint8_t>(index * 7 + 1);

        sequencer.Load([this]()
            {
                sequencer.Step([this]()
                    {
                        flash->ReadBuffer(infra::MakeRange(saved), testAddress, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Step([this]()
                    {
                        flash->EraseSector(testSector, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Step([this]()
                    {
                        flash->ReadBuffer(infra::MakeRange(secondRead), testAddress, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Execute([this]()
                    {
                        erased = IsErased(infra::MakeConstRange(secondRead));
                    });
                sequencer.Step([this]()
                    {
                        flash->WriteBuffer(infra::MakeConstRange(programData), testAddress, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Step([this]()
                    {
                        flash->ReadBuffer(infra::MakeRange(verifyData), testAddress, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Execute([this]()
                    {
                        programmed = verifyData == programData;
                    });
                sequencer.Step([this]()
                    {
                        flash->EraseSector(testSector, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Step([this]()
                    {
                        flash->WriteBuffer(infra::MakeConstRange(saved), testAddress, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Step([this]()
                    {
                        flash->ReadBuffer(infra::MakeRange(secondRead), testAddress, [this]()
                            {
                                sequencer.Continue();
                            });
                    });
                sequencer.Execute([this]()
                    {
                        restored = secondRead == saved;
                        ReportTest();
                    });
            });
    }

    void QuadSpiMemory::ReportTest()
    {
        services::GlobalTracer().Trace() << "QSPI test of the sector at " << infra::hex << testAddress << ": erase " << Result(erased) << ", program " << Result(programmed) << ", restore " << Result(restored);

        busy = false;
        onDone(erased && programmed && restored);
    }
}
