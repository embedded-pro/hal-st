#pragma once

#include "infra/util/ByteRange.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/ble/BondBlobPersistence.hpp"
#include "services/util/ConfigurationStore.hpp"
#include <cstdint>

namespace hal
{
    // The stack's security and GATT records, in the record format of ST's nvm_emul.c. The records are
    // loaded from the persistent copy on construction and written back to it after every change.
    class BleNvmWba
        : public infra::InterfaceConnector<BleNvmWba>
    {
    public:
        BleNvmWba(infra::MemoryRange<uint32_t> records, services::ConfigurationStoreAccess<infra::ByteRange> persistent);

        int Add(uint8_t type, infra::ConstByteRange data, infra::ConstByteRange extraData);
        // As ST's NVM_Get, data may be null to learn how many bytes a read would copy
        int Get(uint8_t mode, uint8_t type, uint16_t offset, uint8_t* data, uint16_t size);
        int Compare(uint16_t offset, infra::ConstByteRange data) const;
        void Discard(uint8_t mode);

    private:
        bool Contained(std::size_t index, std::size_t bytes) const;
        infra::ByteRange RecordData(std::size_t index) const;
        void Changed();

    private:
        infra::MemoryRange<uint32_t> records;
        services::BondBlobPersistence persistence;
        std::size_t current = 0;
        std::size_t warningLevel;
        bool updateScheduled = false;
    };
}
