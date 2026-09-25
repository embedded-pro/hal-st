#include "hal_st/middlewares/ble_middleware/BlePlatformWba.hpp"
#include "hal_st/middlewares/ble_middleware/LinkLayerPlatformWba.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <chrono>
#include <functional>

extern "C"
{
#include "ble_defs.h"
#include "bleplat.h"

    void BleStackCB_Process();
}

namespace
{
    constexpr std::size_t p256PublicKeyWords = 16;
    constexpr std::size_t p256DhKeyWords = 8;

    constexpr uint8_t cmacSubkeyReduction = 0x87;
    constexpr uint8_t cmacPadding = 0x80;

    template<class Block>
    Block ShiftLeftOneBit(const Block& block)
    {
        Block result{};
        uint8_t carry = 0;

        for (auto index = block.size(); index != 0; --index)
        {
            result[index - 1] = static_cast<uint8_t>((block[index - 1] << 1) | carry);
            carry = block[index - 1] >> 7;
        }

        return result;
    }

    // RFC 4493, section 2.3
    template<class Block>
    Block DoubleCmacSubkey(const Block& block)
    {
        auto result = ShiftLeftOneBit(block);

        if ((block.front() & 0x80) != 0)
            result.back() ^= cmacSubkeyReduction;

        return result;
    }
}

namespace hal
{
    BlePlatformWba::BlePlatformWba(infra::MemoryRange<TimerSlot> timers)
        : timers(timers)
    {}

    void BlePlatformWba::Reset()
    {
        for (auto& slot : timers)
        {
            slot.timer.Cancel();
            slot.id = std::nullopt;
        }
    }

    // The stack passes key, input and output least significant byte first, the reverse of AES's byte order
    void BlePlatformWba::EncryptBlock(infra::ConstByteRange key, infra::ConstByteRange input, infra::ByteRange output)
    {
        really_assert(key.size() == blockSize && input.size() == blockSize && output.size() == blockSize);

        Block reversedKey;
        Block reversedInput;
        Block result;

        std::reverse_copy(key.begin(), key.end(), reversedKey.begin());
        std::reverse_copy(input.begin(), input.end(), reversedInput.begin());

        aes.SetKey(reversedKey);
        aes.Encrypt(reversedInput, result);

        std::reverse_copy(result.begin(), result.end(), output.begin());
    }

    void BlePlatformWba::SetCmacKey(infra::ConstByteRange key)
    {
        really_assert(key.size() == blockSize);

        std::copy(key.begin(), key.end(), cmacKey.begin());
        cmacState.fill(0);
    }

    void BlePlatformWba::AppendCmac(infra::ConstByteRange input)
    {
        really_assert(input.size() % blockSize == 0);

        while (!input.empty())
        {
            Block block;
            std::transform(input.begin(), input.begin() + blockSize, cmacState.begin(), block.begin(), std::bit_xor<uint8_t>());
            cmacState = EncryptCmacBlock(block);
            input = infra::DiscardHead(input, blockSize);
        }
    }

    // RFC 4493, section 2.4; the last block is held back from AppendCmac because it is combined with a subkey
    void BlePlatformWba::CalculateCmac(infra::ConstByteRange input, infra::ByteRange tag)
    {
        really_assert(tag.size() == blockSize);

        auto lastBlockSize = input.size() % blockSize;
        if (!input.empty() && lastBlockSize == 0)
            lastBlockSize = blockSize;

        AppendCmac(infra::DiscardTail(input, lastBlockSize));

        Block lastBlock{};
        std::copy(input.end() - lastBlockSize, input.end(), lastBlock.begin());

        auto subkey = DoubleCmacSubkey(EncryptCmacBlock(Block{}));
        if (lastBlockSize < blockSize)
        {
            lastBlock[lastBlockSize] = cmacPadding;
            subkey = DoubleCmacSubkey(subkey);
        }

        for (std::size_t index = 0; index != blockSize; ++index)
            lastBlock[index] ^= cmacState[index] ^ subkey[index];

        auto result = EncryptCmacBlock(lastBlock);
        std::copy(result.begin(), result.end(), tag.begin());
    }

    bool BlePlatformWba::StartTimer(uint16_t id, uint32_t timeoutMs)
    {
        StopTimer(id);

        auto slot = std::find_if(timers.begin(), timers.end(), [](const TimerSlot& slot)
            {
                return !slot.id;
            });

        if (slot == timers.end())
            return false;

        slot->id = id;
        slot->timer.Start(std::chrono::milliseconds(timeoutMs), [this, slot]()
            {
                ExpireTimer(*slot);
            });

        return true;
    }

    void BlePlatformWba::StopTimer(uint16_t id)
    {
        for (auto& slot : timers)
            if (slot.id == id)
            {
                slot.timer.Cancel();
                slot.id = std::nullopt;
            }
    }

    BlePlatformWba::Block BlePlatformWba::EncryptCmacBlock(const Block& input)
    {
        Block result;

        aes.SetKey(cmacKey);
        aes.Encrypt(input, result);

        return result;
    }

    void BlePlatformWba::ExpireTimer(TimerSlot& slot)
    {
        auto id = *slot.id;
        slot.id = std::nullopt;

        BLEPLATCB_TimerExpiry(id);
        BleStackCB_Process();
    }
}

extern "C"
{
    void BLEPLAT_Init()
    {
        hal::BlePlatformWba::Instance().Reset();
    }

    // NVM, PKA and AES-CCM stay stubs until their own step; the basic stack does not use AES-CCM.
    int BLEPLAT_NvmAdd(uint8_t, const uint8_t*, uint16_t, const uint8_t*, uint16_t)
    {
        return BLEPLAT_ERROR;
    }

    int BLEPLAT_NvmGet(uint8_t, uint8_t, uint16_t, uint8_t*, uint16_t)
    {
        return BLEPLAT_EOF;
    }

    int BLEPLAT_NvmCompare(uint16_t, const uint8_t*, uint16_t)
    {
        return BLEPLAT_ERROR;
    }

    void BLEPLAT_NvmDiscard(uint8_t)
    {}

    int BLEPLAT_PkaStartP256Key(const uint32_t*)
    {
        return BLEPLAT_ERROR;
    }

    void BLEPLAT_PkaReadP256Key(uint32_t* local_public_key)
    {
        std::fill_n(local_public_key, p256PublicKeyWords, 0);
    }

    int BLEPLAT_PkaStartDhKey(const uint32_t*, const uint32_t*)
    {
        return BLEPLAT_ERROR;
    }

    int BLEPLAT_PkaReadDhKey(uint32_t* dh_key)
    {
        std::fill_n(dh_key, p256DhKeyWords, 0);
        return BLEPLAT_ERROR;
    }

    int BLEPLAT_AesCcmCrypt(uint8_t, const uint8_t*, uint8_t, const uint8_t*, uint16_t, const uint8_t*, uint32_t, const uint8_t*, uint8_t, uint8_t*, uint8_t*)
    {
        return BLEPLAT_ERROR;
    }

    void BLEPLAT_AesEcbEncrypt(const uint8_t* key, const uint8_t* input, uint8_t* output)
    {
        constexpr std::size_t blockSize = 16;
        hal::BlePlatformWba::Instance().EncryptBlock(infra::ConstByteRange(key, key + blockSize), infra::ConstByteRange(input, input + blockSize), infra::ByteRange(output, output + blockSize));
    }

    void BLEPLAT_AesCmacSetKey(const uint8_t* key)
    {
        constexpr std::size_t blockSize = 16;
        hal::BlePlatformWba::Instance().SetCmacKey(infra::ConstByteRange(key, key + blockSize));
    }

    void BLEPLAT_AesCmacCompute(const uint8_t* input, uint32_t input_length, uint8_t* output_tag)
    {
        constexpr std::size_t blockSize = 16;
        infra::ConstByteRange data(input, input + input_length);

        if (output_tag == nullptr)
            hal::BlePlatformWba::Instance().AppendCmac(data);
        else
            hal::BlePlatformWba::Instance().CalculateCmac(data, infra::ByteRange(output_tag, output_tag + blockSize));
    }

    void BLEPLAT_RngGet(uint8_t n, uint32_t* val)
    {
        hal::LinkLayerPlatformWba::Instance().GenerateRandomData(infra::ReinterpretCastByteRange(infra::MemoryRange<uint32_t>(val, val + n)));
    }

    uint8_t BLEPLAT_TimerStart(uint16_t id, uint32_t timeout)
    {
        return hal::BlePlatformWba::Instance().StartTimer(id, timeout) ? BLE_STATUS_SUCCESS : BLE_STATUS_INSUFFICIENT_RESOURCES;
    }

    void BLEPLAT_TimerStop(uint16_t id)
    {
        hal::BlePlatformWba::Instance().StopTimer(id);
    }
}
