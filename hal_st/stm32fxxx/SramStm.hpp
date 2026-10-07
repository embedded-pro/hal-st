#pragma once

#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_FMC) || defined(HAS_PERIPHERAL_FSMC)

namespace hal
{
    class SramStm
    {
    public:
        enum class Type : uint8_t
        {
            sram,
            psram
        };

        struct Config
        {
            constexpr Config()
            {}

            uint8_t oneBasedBank{ 1 };
            uint32_t size{ 0 };
            Type type{ Type::sram };
            bool writeEnabled{ true };
            FmcStm::NorSramBus bus;
        };

        SramStm(FmcStm& fmc, const Config& config);
        ~SramStm();
        SramStm(const SramStm& other) = delete;
        SramStm& operator=(const SramStm& other) = delete;

        infra::ByteRange Memory() const;

    private:
        SRAM_HandleTypeDef handle{};
        infra::ByteRange memory;
    };
}

#endif
