#pragma once

#include "infra/util/InterfaceConnector.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/ble/BondBlobPersistence.hpp"
#include "services/util/ConfigurationStore.hpp"
#include <cstdint>

namespace hal
{
    class BleNvmWba
        : public infra::InterfaceConnector<BleNvmWba>
    {
    public:
        // The first word of storage tags the format of the persisted blob; the rest is the stack's NVM cache
        BleNvmWba(infra::MemoryRange<uint64_t> storage, services::ConfigurationStoreAccess<infra::ByteRange> persistent);

        infra::MemoryRange<uint64_t> Cache() const;
        void Store();

    private:
        infra::MemoryRange<uint64_t> storage;
        services::BondBlobPersistence persistence;
        bool updateScheduled = false;
    };
}
