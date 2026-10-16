#include "hal_st/stm32fxxx/MmcStm.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_SD) && !defined(SD_EXTERNAL_DMA)

namespace
{
    constexpr uint32_t blocksPer512KiB = 512 * 1024 / 512;
    constexpr uint32_t cacheSizeBytes = 4;

    bool HasCache(const MMC_HandleTypeDef& handle)
    {
        for (uint32_t index = 0; index != cacheSizeBytes; ++index)
            if (hal::detail::MmcHal::ExtCsdByte(handle, hal::detail::MmcHal::cacheSizeIndex + index) != 0)
                return true;

        return false;
    }
}

namespace hal
{
    MmcStm::MmcStm(uint8_t oneBasedIndex, GpioPinStm& clock, GpioPinStm& command, GpioPinStm& data0, GpioPinStm& data1, GpioPinStm& data2, GpioPinStm& data3, const Config& config)
        : SdmmcBlockDeviceStm<detail::MmcHal>{ config }
        , clock{ clock, PinConfigTypeStm::sdClk, oneBasedIndex }
        , command{ command, PinConfigTypeStm::sdCmd, oneBasedIndex }
        , data0{ data0, PinConfigTypeStm::sdD0, oneBasedIndex }
        , data1{ data1, PinConfigTypeStm::sdD1, oneBasedIndex }
        , data2{ data2, PinConfigTypeStm::sdD2, oneBasedIndex }
        , data3{ data3, PinConfigTypeStm::sdD3, oneBasedIndex }
        , oneBasedIndex{ oneBasedIndex }
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralSd.size());

        EnableClockSd(oneBasedIndex - 1);
        InitSdmmc(config);
    }

    MmcStm::~MmcStm()
    {
        Deactivate();
        HAL_MMC_DeInit(&Handle());
        DisableClockSd(oneBasedIndex - 1);
    }

    uint32_t MmcStm::EraseGroupBlocks() const
    {
        return detail::MmcHal::ExtCsdByte(Handle(), detail::MmcHal::highCapacityEraseGroupSizeIndex) * blocksPer512KiB;
    }

    bool MmcStm::CacheEnabled() const
    {
        return (detail::MmcHal::ExtCsdByte(Handle(), detail::MmcHal::cacheControlIndex) & 1) != 0;
    }

    const MmcStm::Identification& MmcStm::DeviceIdentification() const
    {
        return identification;
    }

    void MmcStm::InitSdmmc(const Config& config)
    {
        auto& mmc = Handle();
        auto& init = mmc.Init;

        mmc.Instance = peripheralSd[oneBasedIndex - 1];
        init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
        init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
        // EnableCache reads the EXT_CSD back through the FIFO at full speed; flow control stops the clock instead of overrunning when an interrupt delays the reads
        init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
        init.ClockDiv = config.clockDivider;
        init.BusWide = SDMMC_BUS_WIDE_4B;

        if (HAL_MMC_Init(&mmc) != HAL_OK)
            return;

        HAL_MMC_CardInfoTypeDef info{};

        if (HAL_MMC_GetCardInfo(&mmc, &info) != HAL_OK)
            return;

        really_assert(info.LogBlockSize == blockSize);

        ReadIdentification();

        if (config.cache && !EnableCache(config.transferTimeout))
            return;

        Activate(info.LogBlockNbr, peripheralSdIrq[oneBasedIndex - 1]);
    }

    void MmcStm::ReadIdentification()
    {
        // The HAL parses the CID with the layout of an SD card, so the fields of eMMC are taken from the raw words
        const auto& cid = Handle().CID;

        identification.manufacturer = static_cast<uint8_t>(cid[0] >> 24);
        identification.oem = static_cast<uint8_t>(cid[0] >> 8);
        identification.product = { { static_cast<char>(cid[0]), static_cast<char>(cid[1] >> 24), static_cast<char>(cid[1] >> 16), static_cast<char>(cid[1] >> 8), static_cast<char>(cid[1]), static_cast<char>(cid[2] >> 24) } };
        identification.revision = static_cast<uint8_t>(cid[2] >> 16);
        identification.serialNumber = (cid[2] << 16) | (cid[3] >> 16);
        identification.manufacturingDate = static_cast<uint8_t>(cid[3] >> 8);
    }

    bool MmcStm::EnableCache(std::chrono::milliseconds timeout)
    {
        auto& mmc = Handle();

        if (!HasCache(mmc))
            return true;

        if (detail::MmcHal::Switch(mmc, detail::MmcHal::cacheControlIndex, 1) != HAL_OK)
            return false;

        auto polls = SDMMC_MAX_TRIAL;
        auto state = detail::SdmmcCardState::busy;
        do
        {
            mmc.ErrorCode = HAL_MMC_ERROR_NONE;
            state = detail::MmcHal::CardState(mmc);
        } while (state == detail::SdmmcCardState::busy && mmc.ErrorCode == HAL_MMC_ERROR_NONE && --polls != 0);

        // Read back the EXT_CSD, so that CacheEnabled and Flush see the CACHE_CTRL of the device
        return state == detail::SdmmcCardState::transfer && mmc.ErrorCode == HAL_MMC_ERROR_NONE && HAL_MMC_GetCardExtCSD(&mmc, mmc.Ext_CSD, static_cast<uint32_t>(timeout.count())) == HAL_OK;
    }
}

#endif
