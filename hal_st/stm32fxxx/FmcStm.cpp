#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>
#include <limits>

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

// The F4 FSMC HAL only aliases a subset of its NORSRAM constants to their FMC names.
#if defined(HAS_PERIPHERAL_FSMC)
#define HALST_FMC(name) FSMC_##name
#else
#define HALST_FMC(name) FMC_##name
#endif

namespace
{
    static_assert(hal::FmcStm::norSramBase == NOR_MEMORY_ADRESS1);
    static_assert(hal::FmcStm::norSramBase + hal::FmcStm::norSramBankSize == NOR_MEMORY_ADRESS2);
    static_assert(hal::FmcStm::norSramBase + 2 * hal::FmcStm::norSramBankSize == NOR_MEMORY_ADRESS3);
    static_assert(hal::FmcStm::norSramBase + 3 * hal::FmcStm::norSramBankSize == NOR_MEMORY_ADRESS4);

    constexpr std::array<uint32_t, 4> norSramBanks{
        HALST_FMC(NORSRAM_BANK1),
        HALST_FMC(NORSRAM_BANK2),
        HALST_FMC(NORSRAM_BANK3),
        HALST_FMC(NORSRAM_BANK4)
    };

    constexpr std::array<uint32_t, 4> accessModes{
        HALST_FMC(ACCESS_MODE_A),
        HALST_FMC(ACCESS_MODE_B),
        HALST_FMC(ACCESS_MODE_C),
        HALST_FMC(ACCESS_MODE_D)
    };

    uint32_t MemoryTypeValue(hal::FmcStm::MemoryType type)
    {
        switch (type)
        {
            case hal::FmcStm::MemoryType::sram:
                return HALST_FMC(MEMORY_TYPE_SRAM);
            case hal::FmcStm::MemoryType::psram:
                return HALST_FMC(MEMORY_TYPE_PSRAM);
            default:
                return HALST_FMC(MEMORY_TYPE_NOR);
        }
    }

    uint32_t BusWidthValue(uint8_t widthBits)
    {
        switch (widthBits)
        {
            case 8:
                return HALST_FMC(NORSRAM_MEM_BUS_WIDTH_8);
            case 16:
                return HALST_FMC(NORSRAM_MEM_BUS_WIDTH_16);
            default:
                really_assert(widthBits == 32);
                return HALST_FMC(NORSRAM_MEM_BUS_WIDTH_32);
        }
    }

    uint32_t PageSizeValue(hal::FmcStm::PageSize pageSize)
    {
        switch (pageSize)
        {
            case hal::FmcStm::PageSize::none:
                return HALST_FMC(PAGE_SIZE_NONE);
            case hal::FmcStm::PageSize::bytes128:
                return HALST_FMC(PAGE_SIZE_128);
            case hal::FmcStm::PageSize::bytes256:
                return HALST_FMC(PAGE_SIZE_256);
            case hal::FmcStm::PageSize::bytes512:
                return HALST_FMC(PAGE_SIZE_512);
            default:
                return HALST_FMC(PAGE_SIZE_1024);
        }
    }
}

namespace hal
{
    FmcStm::FmcStm(MultiGpioPinStm& pins)
        : pins(pins, PinConfigTypeStm::fmc, 0)
    {
#if defined(HAS_PERIPHERAL_FMC)
        EnableClockFmc(0);
#else
        EnableClockFsmc(0);
#endif
    }

    FmcStm::~FmcStm()
    {
#if defined(HAS_PERIPHERAL_FMC)
        DisableClockFmc(0);
#else
        DisableClockFsmc(0);
#endif
    }

    uint32_t FmcStm::NorSramWindow(uint8_t oneBasedBank)
    {
        really_assert(oneBasedBank >= 1 && oneBasedBank <= norSramBanks.size());
        return norSramBase + (oneBasedBank - 1) * norSramBankSize;
    }

    FMC_NORSRAM_InitTypeDef FmcStm::CreateNorSramInit(uint8_t oneBasedBank, MemoryType type, bool writeEnabled, const NorSramBus& bus)
    {
        really_assert(oneBasedBank >= 1 && oneBasedBank <= norSramBanks.size());

        FMC_NORSRAM_InitTypeDef result{};

        result.NSBank = norSramBanks[oneBasedBank - 1];
        result.DataAddressMux = bus.addressDataMultiplexed ? HALST_FMC(DATA_ADDRESS_MUX_ENABLE) : HALST_FMC(DATA_ADDRESS_MUX_DISABLE);
        result.MemoryType = MemoryTypeValue(type);
        result.MemoryDataWidth = BusWidthValue(bus.widthBits);
        result.BurstAccessMode = bus.burstAccess ? HALST_FMC(BURST_ACCESS_MODE_ENABLE) : HALST_FMC(BURST_ACCESS_MODE_DISABLE);
        result.WaitSignalPolarity = bus.waitSignalActiveHigh ? HALST_FMC(WAIT_SIGNAL_POLARITY_HIGH) : HALST_FMC(WAIT_SIGNAL_POLARITY_LOW);
        result.WaitSignalActive = bus.waitSignalDuringWaitState ? HALST_FMC(WAIT_TIMING_DURING_WS) : HALST_FMC(WAIT_TIMING_BEFORE_WS);
        result.WriteOperation = writeEnabled ? HALST_FMC(WRITE_OPERATION_ENABLE) : HALST_FMC(WRITE_OPERATION_DISABLE);
        result.WaitSignal = bus.waitSignal ? HALST_FMC(WAIT_SIGNAL_ENABLE) : HALST_FMC(WAIT_SIGNAL_DISABLE);
        result.ExtendedMode = bus.writeTiming ? HALST_FMC(EXTENDED_MODE_ENABLE) : HALST_FMC(EXTENDED_MODE_DISABLE);
        result.AsynchronousWait = bus.asynchronousWait ? HALST_FMC(ASYNCHRONOUS_WAIT_ENABLE) : HALST_FMC(ASYNCHRONOUS_WAIT_DISABLE);
        result.WriteBurst = bus.writeBurst ? HALST_FMC(WRITE_BURST_ENABLE) : HALST_FMC(WRITE_BURST_DISABLE);
        result.PageSize = PageSizeValue(bus.pageSize);
#if defined(FMC_BCR1_WFDIS)
        result.WriteFifo = bus.writeFifo ? FMC_WRITE_FIFO_ENABLE : FMC_WRITE_FIFO_DISABLE;
#endif

        return result;
    }

    FMC_NORSRAM_TimingTypeDef FmcStm::CreateNorSramTiming(const NorSramTiming& timing)
    {
        really_assert(timing.addressSetup <= 15);
        really_assert(timing.addressHold >= 1 && timing.addressHold <= 15);
        really_assert(timing.dataSetup >= 1);
        really_assert(timing.busTurnaround <= 15);
        really_assert(timing.clockDivision >= 2 && timing.clockDivision <= 16);
        really_assert(timing.dataLatency >= 2 && timing.dataLatency <= 17);
        really_assert(static_cast<std::size_t>(timing.accessMode) < accessModes.size());

        FMC_NORSRAM_TimingTypeDef result{};

        result.AddressSetupTime = timing.addressSetup;
        result.AddressHoldTime = timing.addressHold;
        result.DataSetupTime = timing.dataSetup;
        result.BusTurnAroundDuration = timing.busTurnaround;
        result.CLKDivision = timing.clockDivision;
        result.DataLatency = timing.dataLatency;
        result.AccessMode = accessModes[static_cast<std::size_t>(timing.accessMode)];

        return result;
    }

    void FmcStm::DelayAtLeast(std::chrono::microseconds duration)
    {
        // HAL_Delay cannot be used: the tick only advances once the event infrastructure runs.
        const uint64_t iterations = static_cast<uint64_t>(duration.count()) * (SystemCoreClock / 1000000);
        volatile uint32_t remaining = static_cast<uint32_t>(std::min<uint64_t>(iterations, std::numeric_limits<uint32_t>::max()));

        while (remaining != 0)
            remaining = remaining - 1;
    }
}

#undef HALST_FMC

#endif
