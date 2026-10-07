#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_SDRAM) && defined(HAS_PERIPHERAL_FMC)

namespace
{
    constexpr uint32_t commandTimeout = 0xFFFF;
    constexpr uint32_t maximumRefreshCount = 0x1FFF;
    constexpr uint32_t modeRegisterSingleLocationWrite = 0x0200;
    constexpr uint32_t modeRegisterCasLatencyShift = 4;

    infra::ByteRange Window(const hal::SdRamStm::Config& config)
    {
        really_assert(config.bank == 1 || config.bank == 2);
        really_assert(config.size != 0 && config.size <= hal::FmcStm::sdramBankSize);

        const uint32_t defaultBegin = config.bank == 1 ? hal::FmcStm::sdramBank1Base : hal::FmcStm::sdramBank2Base;
        const uint32_t begin = config.address != 0 ? config.address : defaultBegin;
        return infra::ByteRange(reinterpret_cast<uint8_t*>(begin), reinterpret_cast<uint8_t*>(begin + config.size));
    }

    uint32_t ColumnBits(uint8_t columnBits)
    {
        switch (columnBits)
        {
            case 8:
                return FMC_SDRAM_COLUMN_BITS_NUM_8;
            case 9:
                return FMC_SDRAM_COLUMN_BITS_NUM_9;
            case 10:
                return FMC_SDRAM_COLUMN_BITS_NUM_10;
            default:
                really_assert(columnBits == 11);
                return FMC_SDRAM_COLUMN_BITS_NUM_11;
        }
    }

    uint32_t RowBits(uint8_t rowBits)
    {
        switch (rowBits)
        {
            case 11:
                return FMC_SDRAM_ROW_BITS_NUM_11;
            case 12:
                return FMC_SDRAM_ROW_BITS_NUM_12;
            default:
                really_assert(rowBits == 13);
                return FMC_SDRAM_ROW_BITS_NUM_13;
        }
    }

    uint32_t BusWidth(uint8_t busWidth)
    {
        switch (busWidth)
        {
            case 8:
                return FMC_SDRAM_MEM_BUS_WIDTH_8;
            case 16:
                return FMC_SDRAM_MEM_BUS_WIDTH_16;
            default:
#if defined(FMC_SDRAM_MEM_BUS_WIDTH_32)
                really_assert(busWidth == 32);
                return FMC_SDRAM_MEM_BUS_WIDTH_32;
#else
                really_assert(false);
                return FMC_SDRAM_MEM_BUS_WIDTH_16;
#endif
        }
    }

    uint32_t CasLatency(uint8_t casLatency)
    {
        switch (casLatency)
        {
            case 1:
                return FMC_SDRAM_CAS_LATENCY_1;
            case 2:
                return FMC_SDRAM_CAS_LATENCY_2;
            default:
                really_assert(casLatency == 3);
                return FMC_SDRAM_CAS_LATENCY_3;
        }
    }

    uint32_t ReadPipeDelay(uint8_t readPipeDelay)
    {
        switch (readPipeDelay)
        {
            case 0:
                return FMC_SDRAM_RPIPE_DELAY_0;
            case 1:
                return FMC_SDRAM_RPIPE_DELAY_1;
            default:
                really_assert(readPipeDelay == 2);
                return FMC_SDRAM_RPIPE_DELAY_2;
        }
    }

    uint32_t BurstLengthCode(uint8_t burstLength)
    {
        switch (burstLength)
        {
            case 1:
                return 0;
            case 2:
                return 1;
            case 4:
                return 2;
            default:
                really_assert(burstLength == 8);
                return 3;
        }
    }

    uint32_t Cycles(uint8_t cycles)
    {
        really_assert(cycles >= 1 && cycles <= 16);
        return cycles;
    }
}

namespace hal
{
    SdRamStm::SdRamStm(FmcStm&, const Config& config)
        : memory(Window(config))
    {
        Initialize(config);
    }

    SdRamStm::SdRamStm(MultiGpioPinStm& sdramPins, const Config& config)
        : ownedFmc(std::in_place, sdramPins)
        , memory(Window(config))
    {
        Initialize(config);
    }

    SdRamStm::~SdRamStm()
    {
        HAL_SDRAM_DeInit(&handle);
    }

    infra::ByteRange SdRamStm::Memory() const
    {
        return memory;
    }

    void SdRamStm::SanityCheck()
    {
        really_assert(memory.size() > 0x50);

        const_cast<volatile uint8_t&>(memory[0x50]) = 0x45;
        really_assert(const_cast<volatile uint8_t&>(memory[0x50]) == 0x45);
    }

    void SdRamStm::Initialize(const Config& config)
    {
        really_assert(config.refreshCount != 0 && config.refreshCount <= maximumRefreshCount);
        really_assert(config.internalBanks == 2 || config.internalBanks == 4);
        really_assert(config.sdClockDivider == 2 || config.sdClockDivider == 3);
        really_assert(config.autoRefreshCycles >= 1 && config.autoRefreshCycles <= 15);

        handle.Instance = FMC_SDRAM_DEVICE;
        handle.Init.SDBank = config.bank == 1 ? FMC_SDRAM_BANK1 : FMC_SDRAM_BANK2;
        handle.Init.ColumnBitsNumber = ColumnBits(config.columnBits);
        handle.Init.RowBitsNumber = RowBits(config.rowBits);
        handle.Init.MemoryDataWidth = BusWidth(config.busWidth);
        handle.Init.InternalBankNumber = config.internalBanks == 2 ? FMC_SDRAM_INTERN_BANKS_NUM_2 : FMC_SDRAM_INTERN_BANKS_NUM_4;
        handle.Init.CASLatency = CasLatency(config.casLatency);
        handle.Init.WriteProtection = config.writeProtection ? FMC_SDRAM_WRITE_PROTECTION_ENABLE : FMC_SDRAM_WRITE_PROTECTION_DISABLE;
        handle.Init.SDClockPeriod = config.sdClockDivider == 2 ? FMC_SDRAM_CLOCK_PERIOD_2 : FMC_SDRAM_CLOCK_PERIOD_3;
        handle.Init.ReadBurst = config.readBurst ? FMC_SDRAM_RBURST_ENABLE : FMC_SDRAM_RBURST_DISABLE;
        handle.Init.ReadPipeDelay = ReadPipeDelay(config.readPipeDelay);

        FMC_SDRAM_TimingTypeDef timing{};
        timing.LoadToActiveDelay = Cycles(config.timing.loadToActiveDelay);
        timing.ExitSelfRefreshDelay = Cycles(config.timing.exitSelfRefreshDelay);
        timing.SelfRefreshTime = Cycles(config.timing.selfRefreshTime);
        timing.RowCycleDelay = Cycles(config.timing.rowCycleDelay);
        timing.WriteRecoveryTime = Cycles(config.timing.writeRecoveryTime);
        timing.RPDelay = Cycles(config.timing.rowPrechargeDelay);
        timing.RCDDelay = Cycles(config.timing.rowToColumnDelay);

        auto result = HAL_SDRAM_Init(&handle, &timing);
        really_assert(result == HAL_OK);

        commandTarget = config.bank == 1 ? FMC_SDRAM_CMD_TARGET_BANK1 : FMC_SDRAM_CMD_TARGET_BANK2;

        SendCommand(FMC_SDRAM_CMD_CLK_ENABLE, 1, 0);
        FmcStm::DelayAtLeast(config.powerUpDelay);
        SendCommand(FMC_SDRAM_CMD_PALL, 1, 0);
        SendCommand(FMC_SDRAM_CMD_AUTOREFRESH_MODE, config.autoRefreshCycles, 0);
        SendCommand(FMC_SDRAM_CMD_LOAD_MODE, 1, modeRegisterSingleLocationWrite | (static_cast<uint32_t>(config.casLatency) << modeRegisterCasLatencyShift) | BurstLengthCode(config.burstLength));

        result = HAL_SDRAM_ProgramRefreshRate(&handle, config.refreshCount);
        really_assert(result == HAL_OK);
    }

    void SdRamStm::SendCommand(uint32_t mode, uint32_t autoRefreshNumber, uint32_t modeRegister)
    {
        FMC_SDRAM_CommandTypeDef command = {};
        command.CommandMode = mode;
        command.CommandTarget = commandTarget;
        command.AutoRefreshNumber = autoRefreshNumber;
        command.ModeRegisterDefinition = modeRegister;

        auto result = HAL_SDRAM_SendCommand(&handle, &command, commandTimeout);
        really_assert(result == HAL_OK);
    }
}

#endif
