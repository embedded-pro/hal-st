#include "hal_st/middlewares/ble_middleware/BleNvmWba.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

extern "C"
{
#include "bleplat.h"
}

namespace
{
    constexpr uint32_t validRecord = 0x01000000;
    constexpr uint8_t noRecordType = 0xff;
    constexpr std::size_t maxMergedRecordWords = 0x4000;

    bool Blank(uint32_t header)
    {
        return header == 0;
    }

    uint16_t Size(uint32_t header)
    {
        return header & 0xffff;
    }

    uint8_t Type(uint32_t header)
    {
        return (header >> 16) & 0xff;
    }

    bool Valid(uint32_t header)
    {
        return ((header >> 24) & 0xff) != 0;
    }

    // A record is its header word followed by its data, padded to whole words
    std::size_t Words(std::size_t size)
    {
        return (size + sizeof(uint32_t) + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    }

    services::ConfigurationStoreAccess<infra::ByteRange> SizeChecked(services::ConfigurationStoreAccess<infra::ByteRange> persistent, infra::MemoryRange<uint32_t> records)
    {
        really_assert(records.size() >= 2 && persistent->size() == records.size() * sizeof(uint32_t));
        return persistent;
    }
}

namespace hal
{
    BleNvmWba::BleNvmWba(infra::MemoryRange<uint32_t> records, services::ConfigurationStoreAccess<infra::ByteRange> persistent)
        : records(records)
        , persistence(SizeChecked(persistent, records), infra::ReinterpretCastByteRange(records))
        , warningLevel(records.size() - 1)
    {
        // As ST's app_ble.c: no record has type 0xff, so a sound store reads to its end
        if (Get(BLEPLAT_NVM_FIRST, noRecordType, 0, nullptr, 0) != BLEPLAT_EOF)
            Discard(BLEPLAT_NVM_ALL);
    }

    int BleNvmWba::Add(uint8_t type, infra::ConstByteRange data, infra::ConstByteRange extraData)
    {
        if (data.empty())
            return BLEPLAT_OK;

        auto totalSize = data.size() + extraData.size();
        auto requiredWords = 1 + Words(totalSize);
        std::size_t index = 0;
        std::size_t removed = 0;

        while (true)
        {
            index = 0;
            removed = 0;
            auto left = records.size();

            while (!Blank(records[index]))
            {
                auto next = Words(Size(records[index]));
                if (next >= left)
                    return BLEPLAT_ERROR;

                if (!Valid(records[index]))
                    removed += next;

                index += next;
                left -= next;
            }

            if (requiredWords <= left)
                break;

            if (removed == 0)
                return BLEPLAT_FULL;

            index = 0;
            left = records.size();
            while (!Blank(records[index]) && Valid(records[index]))
            {
                auto next = Words(Size(records[index]));
                index += next;
                left -= next;
            }

            if (Blank(records[index]))
                return BLEPLAT_FULL;

            auto next = Words(Size(records[index]));
            std::copy(records.begin() + index + next, records.begin() + index + left, records.begin() + index);
            Changed();
        }

        records[index] = validRecord | (static_cast<uint32_t>(type) << 16) | totalSize;
        auto recordData = RecordData(index);
        std::copy(data.begin(), data.end(), recordData.begin());
        std::copy(extraData.begin(), extraData.end(), recordData.begin() + data.size());

        index += Words(totalSize);
        records[index] = 0;
        Changed();

        warningLevel = std::min(warningLevel, records.size() + 1 - requiredWords);
        if (index + 1 - removed > warningLevel)
            return BLEPLAT_WARN;

        return BLEPLAT_OK;
    }

    int BleNvmWba::Get(uint8_t mode, uint8_t type, uint16_t offset, uint8_t* data, uint16_t size)
    {
        if (mode == BLEPLAT_NVM_FIRST)
            current = 0;
        else if (current >= records.size() - 1)
        {
            current = 0;
            return BLEPLAT_EOF;
        }

        if (mode != BLEPLAT_NVM_CURRENT)
        {
            if (mode == BLEPLAT_NVM_NEXT)
            {
                current += Words(Size(records[current]));
                if (current >= records.size())
                    return BLEPLAT_ERROR;
            }

            while (!(Blank(records[current]) || (Valid(records[current]) && Type(records[current]) == type)))
            {
                current += Words(Size(records[current]));
                if (current >= records.size())
                    return BLEPLAT_ERROR;
            }
        }

        if (Blank(records[current]))
            return BLEPLAT_EOF;

        if (!(Valid(records[current]) && Type(records[current]) == type))
            return BLEPLAT_ERROR;

        auto remaining = static_cast<int>(Size(records[current])) - static_cast<int>(offset);
        if (remaining <= 0)
            return 0;

        auto copySize = std::min<int>(size, remaining);
        if (!Contained(current, offset + copySize))
            return BLEPLAT_ERROR;

        if (data != nullptr)
            std::copy_n(RecordData(current).begin() + offset, copySize, data);

        return copySize;
    }

    int BleNvmWba::Compare(uint16_t offset, infra::ConstByteRange data) const
    {
        if (current >= records.size() - 1 || Blank(records[current]))
            return BLEPLAT_EOF;

        auto remaining = static_cast<int>(Size(records[current])) - static_cast<int>(offset);
        if (static_cast<int>(data.size()) <= remaining && !Contained(current, offset + data.size()))
            return BLEPLAT_ERROR;

        if (static_cast<int>(data.size()) > remaining || !std::equal(data.begin(), data.end(), RecordData(current).begin() + offset))
            return static_cast<int>(data.size());

        return BLEPLAT_OK;
    }

    void BleNvmWba::Discard(uint8_t mode)
    {
        if (mode == BLEPLAT_NVM_CURRENT)
        {
            if (current >= records.size() - 1)
            {
                current = 0;
                return;
            }

            if (!Blank(records[current]))
            {
                auto next = Words(Size(records[current]));
                if (current + next >= records.size())
                    return;

                auto following = records[current + next];
                if (Blank(following))
                    records[current] = 0;
                else
                {
                    // An invalid record keeps its size, so that the records after it can still be found;
                    // merging it with an invalid successor keeps the chain short
                    uint32_t size = Size(records[current]);
                    if (!Valid(following))
                    {
                        next += Words(Size(following));
                        if (next <= maxMergedRecordWords)
                            size = (next - 1) * sizeof(uint32_t);
                    }

                    records[current] = size;
                }
            }
        }
        else if (mode == BLEPLAT_NVM_ALL)
            records[0] = 0;

        Changed();
    }

    // After a compaction or a full discard the current position can point into stale data, which ST's
    // nvm_emul.c then reads beyond the store
    bool BleNvmWba::Contained(std::size_t index, std::size_t bytes) const
    {
        return RecordData(index).size() >= bytes;
    }

    infra::ByteRange BleNvmWba::RecordData(std::size_t index) const
    {
        return infra::DiscardHead(infra::ReinterpretCastByteRange(records), (index + 1) * sizeof(uint32_t));
    }

    // The stack writes several records in a row, after pairing for instance, which are persisted together
    void BleNvmWba::Changed()
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
