#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

namespace hal
{
    // Owns the FSMC/FMC clock and every pin of the external bus. Memory drivers take a reference to it,
    // so they must be destroyed before it.
    class FmcStm
    {
    public:
        static constexpr uint32_t norSramBase = 0x60000000;
        static constexpr uint32_t norSramBankSize = 0x04000000;
        static constexpr uint32_t sdramBank1Base = 0xC0000000;
        static constexpr uint32_t sdramBank2Base = 0xD0000000;
        static constexpr uint32_t sdramBankSize = 0x10000000;

        enum class MemoryType : uint8_t
        {
            sram,
            psram,
            nor
        };

        enum class AccessMode : uint8_t
        {
            a,
            b,
            c,
            d
        };

        enum class PageSize : uint16_t
        {
            none = 0,
            bytes128 = 128,
            bytes256 = 256,
            bytes512 = 512,
            bytes1024 = 1024
        };

        struct NorSramTiming
        {
            constexpr NorSramTiming()
            {}

            // In HCLK cycles. The defaults are slow but valid for every device and family.
            uint8_t addressSetup{ 15 };
            uint8_t addressHold{ 15 };
            uint8_t dataSetup{ 255 };
            uint8_t busTurnaround{ 15 };
            uint8_t clockDivision{ 16 };
            uint8_t dataLatency{ 17 };
            AccessMode accessMode{ AccessMode::a };
        };

        struct NorSramBus
        {
            constexpr NorSramBus()
            {}

            uint8_t widthBits{ 16 };
            bool addressDataMultiplexed{ false };
            bool burstAccess{ false };
            bool waitSignal{ false };
            bool waitSignalActiveHigh{ false };
            bool waitSignalDuringWaitState{ false };
            bool asynchronousWait{ false };
            bool writeBurst{ false };
            bool writeFifo{ true };
            PageSize pageSize{ PageSize::none };
            NorSramTiming timing;
            std::optional<NorSramTiming> writeTiming;
        };

        explicit FmcStm(MultiGpioPinStm& pins);
        ~FmcStm();
        FmcStm(const FmcStm& other) = delete;
        FmcStm& operator=(const FmcStm& other) = delete;

        static uint32_t NorSramWindow(uint8_t oneBasedBank);
        static FMC_NORSRAM_InitTypeDef CreateNorSramInit(uint8_t oneBasedBank, MemoryType type, bool writeEnabled, const NorSramBus& bus);
        static FMC_NORSRAM_TimingTypeDef CreateNorSramTiming(const NorSramTiming& timing);
        static void DelayAtLeast(std::chrono::microseconds duration);

    private:
        MultiPeripheralPinStm pins;
    };
}

#endif
