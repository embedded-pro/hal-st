#include "demo/stm32h757i_eval/QuadSpiMemory.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <algorithm>

namespace main_
{
    namespace
    {
        constexpr uint8_t commandReadJedecId = 0x9f;

        hal::QuadSpi::Header JedecHeader()
        {
            return hal::QuadSpi::Header{ std::make_optional<uint8_t>(commandReadJedecId), {}, {}, 0 };
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
        this->onDone = onDone;

        if (!flash)
        {
            this->onDone(false);
            return;
        }

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
        this->onDone = onDone;

        if (!flash)
        {
            this->onDone(false);
            return;
        }

        const uint32_t lastSector = flash->NumberOfSectors() - 1;
        const uint32_t address = flash->AddressOfSector(lastSector);

        for (std::size_t index = 0; index != programData.size(); ++index)
            programData[index] = static_cast<uint8_t>(index * 7 + 1);

        flash->EraseSector(lastSector, [this, address]()
            {
                flash->WriteBuffer(infra::MakeConstRange(programData), address, [this, address]()
                    {
                        flash->ReadBuffer(infra::MakeRange(verifyData), address, [this, address]()
                            {
                                const bool match = verifyData == programData;
                                services::GlobalTracer().Trace() << "QSPI erase, program and verify of the sector at " << infra::hex << address << ": " << (match ? "ok" : "mismatch");
                                this->onDone(match);
                            });
                    });
            });
    }

    void QuadSpiMemory::GeometryReady()
    {
        flash.emplace(spi, geometry);

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
        const std::size_t blank = static_cast<std::size_t>(std::count(firstRead.begin(), firstRead.end(), uint8_t{ 0xff }));
        const bool identified = identification[0] != 0x00 && identification[0] != 0xff;

        services::GlobalTracer().Trace() << "QSPI " << Size() / (1024 * 1024) << " MB, first " << static_cast<uint32_t>(checkBytes) << " bytes read twice: " << (stable ? "stable" : "unstable") << ", " << static_cast<uint32_t>(blank) << " blank, JEDEC " << infra::hex << JedecId();

        onDone(identified && stable);
    }
}
