#pragma once

#include DEVICE_HEADER
#include "hal/interfaces/BackupRam.hpp"

namespace hal
{
    class BackupRamStm
        : public hal::BackupRam<volatile uint32_t>
    {
    public:
        BackupRamStm();

        infra::MemoryRange<volatile uint32_t> Get() const override;
    };
}
