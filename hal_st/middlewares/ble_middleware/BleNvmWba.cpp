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

    bool Matches(uint32_t header, uint8_t type)
    {
        return Valid(header) && Type(header) == type;
    }

    // A record is its header word followed by its data, padded to whole words
    std::size_t Words(std::size_t size)
    {
        return (size + sizeof(uint32_t) + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    }

    // A discarded record keeps its size so that the records after it can still be found; merging it with a
    // discarded successor keeps the chain short
    uint32_t DiscardedHeader(uint32_t header, uint32_t following, std::size_t words)
    {
        if (Blank(following))
            return 0;

        if (Valid(following))
            return Size(header);

        words += Words(Size(following));
        return words <= maxMergedRecordWords ? (words - 1) * sizeof(uint32_t) : Size(header);
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

        auto requiredWords = 1 + Words(data.size() + extraData.size());

        while (true)
        {
            auto layout = Scan();
            if (!layout)
                return BLEPLAT_ERROR;

            if (requiredWords <= records.size() - layout->end)
                return Write(*layout, type, data, extraData);

            if (layout->invalidWords == 0 || !RemoveFirstInvalidRecord())
                return BLEPLAT_FULL;
        }
    }

    int BleNvmWba::Get(uint8_t mode, uint8_t type, uint16_t offset, uint8_t* data, uint16_t size)
    {
        auto status = Seek(mode, type);
        if (status != BLEPLAT_OK)
            return status;

        return Read(offset, data, size);
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
        if (mode == BLEPLAT_NVM_CURRENT && !DiscardCurrent())
            return;

        if (mode == BLEPLAT_NVM_ALL)
            records[0] = 0;

        Changed();
    }

    std::optional<BleNvmWba::Layout> BleNvmWba::Scan() const
    {
        Layout layout{ 0, 0 };

        while (!Blank(records[layout.end]))
        {
            auto words = Words(Size(records[layout.end]));
            if (words >= records.size() - layout.end)
                return std::nullopt;

            if (!Valid(records[layout.end]))
                layout.invalidWords += words;

            layout.end += words;
        }

        return layout;
    }

    bool BleNvmWba::RemoveFirstInvalidRecord()
    {
        std::size_t index = 0;
        while (!Blank(records[index]) && Valid(records[index]))
            index += Words(Size(records[index]));

        if (Blank(records[index]))
            return false;

        std::copy(records.begin() + index + Words(Size(records[index])), records.end(), records.begin() + index);
        Changed();
        return true;
    }

    int BleNvmWba::Write(const Layout& layout, uint8_t type, infra::ConstByteRange data, infra::ConstByteRange extraData)
    {
        auto size = data.size() + extraData.size();
        records[layout.end] = validRecord | (static_cast<uint32_t>(type) << 16) | size;

        auto recordData = RecordData(layout.end);
        std::copy(data.begin(), data.end(), recordData.begin());
        std::copy(extraData.begin(), extraData.end(), recordData.begin() + data.size());

        auto end = layout.end + Words(size);
        records[end] = 0;
        Changed();

        warningLevel = std::min(warningLevel, records.size() - Words(size));
        return end + 1 - layout.invalidWords > warningLevel ? BLEPLAT_WARN : BLEPLAT_OK;
    }

    int BleNvmWba::Seek(uint8_t mode, uint8_t type)
    {
        if (mode == BLEPLAT_NVM_FIRST)
            current = 0;
        else if (current >= records.size() - 1)
        {
            current = 0;
            return BLEPLAT_EOF;
        }

        if (mode == BLEPLAT_NVM_NEXT && !Advance())
            return BLEPLAT_ERROR;

        if (mode != BLEPLAT_NVM_CURRENT)
            while (!Blank(records[current]) && !Matches(records[current], type))
                if (!Advance())
                    return BLEPLAT_ERROR;

        if (Blank(records[current]))
            return BLEPLAT_EOF;

        return Matches(records[current], type) ? BLEPLAT_OK : BLEPLAT_ERROR;
    }

    bool BleNvmWba::Advance()
    {
        current += Words(Size(records[current]));
        return current < records.size();
    }

    int BleNvmWba::Read(uint16_t offset, uint8_t* data, uint16_t size) const
    {
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

    bool BleNvmWba::DiscardCurrent()
    {
        if (current >= records.size() - 1)
        {
            current = 0;
            return false;
        }

        if (Blank(records[current]))
            return true;

        auto words = Words(Size(records[current]));
        if (current + words >= records.size())
            return false;

        records[current] = DiscardedHeader(records[current], records[current + words], words);
        return true;
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
