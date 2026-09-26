#include "hal_st/middlewares/ble_middleware/BleNvmWba.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace
{
    // Blobs written by the record based NVM of stack versions before 2.8 do not carry this tag, and the
    // cache based stack would misread them
    constexpr uint64_t cacheFormatTag = 0x0000000132484341;

    services::ConfigurationStoreAccess<infra::ByteRange> SizeChecked(services::ConfigurationStoreAccess<infra::ByteRange> persistent, infra::MemoryRange<uint64_t> storage)
    {
        really_assert(storage.size() >= 2 && persistent->size() == storage.size() * sizeof(uint64_t));
        return persistent;
    }
}

namespace hal
{
    BleNvmWba::BleNvmWba(infra::MemoryRange<uint64_t> storage, services::ConfigurationStoreAccess<infra::ByteRange> persistent)
        : storage(storage)
        , persistence(SizeChecked(persistent, storage), infra::ReinterpretCastByteRange(storage))
    {
        if (storage.front() != cacheFormatTag)
        {
            std::fill(storage.begin(), storage.end(), 0);
            storage.front() = cacheFormatTag;
        }
    }

    infra::MemoryRange<uint64_t> BleNvmWba::Cache() const
    {
        return infra::DiscardHead(storage, 1);
    }

    void BleNvmWba::Store()
    {
        if (!updateScheduled)
        {
            updateScheduled = true;
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    updateScheduled = false;
                    persistence.Update();
                });
        }
    }
}
