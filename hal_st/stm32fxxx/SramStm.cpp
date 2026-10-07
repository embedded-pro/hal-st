#include "hal_st/stm32fxxx/SramStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

namespace
{
    infra::ByteRange Window(const hal::SramStm::Config& config)
    {
        really_assert(config.size != 0 && config.size <= hal::FmcStm::norSramBankSize);

        const auto begin = hal::FmcStm::NorSramWindow(config.oneBasedBank);
        return infra::ByteRange(reinterpret_cast<uint8_t*>(begin), reinterpret_cast<uint8_t*>(begin + config.size));
    }
}

namespace hal
{
    SramStm::SramStm(FmcStm&, const Config& config)
        : memory(Window(config))
    {
        handle.Instance = FMC_NORSRAM_DEVICE;
        handle.Extended = FMC_NORSRAM_EXTENDED_DEVICE;
        handle.Init = FmcStm::CreateNorSramInit(config.oneBasedBank, config.type == Type::sram ? FmcStm::MemoryType::sram : FmcStm::MemoryType::psram, config.writeEnabled, config.bus);

        auto timing = FmcStm::CreateNorSramTiming(config.bus.timing);
        auto writeTiming = config.bus.writeTiming ? FmcStm::CreateNorSramTiming(*config.bus.writeTiming) : timing;

        auto result = HAL_SRAM_Init(&handle, &timing, &writeTiming);
        really_assert(result == HAL_OK);
    }

    SramStm::~SramStm()
    {
        HAL_SRAM_DeInit(&handle);
#if defined(FMC_BCR1_FMCEN)
        // Deinitialising bank 1 resets the controller-wide enable, which other banks still depend on.
        __FMC_ENABLE();
#endif
    }

    infra::ByteRange SramStm::Memory() const
    {
        return memory;
    }
}

#endif
