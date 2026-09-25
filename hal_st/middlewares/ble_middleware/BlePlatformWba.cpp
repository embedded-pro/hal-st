#include "hal_st/middlewares/ble_middleware/BlePlatformWba.hpp"
#include "hal_st/middlewares/ble_middleware/LinkLayerPlatformWba.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "services/crypto/Secp256r1.hpp"
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
    constexpr std::size_t p256KeyWords = 8;

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
    BlePlatformWba::BlePlatformWba(infra::MemoryRange<TimerSlot> timers, AesCreator& aesCreator, PkaCreator& pkaCreator)
        : timers(timers)
        , aesCreator(aesCreator)
        , pkaCreator(pkaCreator)
    {}

    void BlePlatformWba::Reset()
    {
        for (auto& slot : timers)
        {
            slot.timer.Cancel();
            slot.id = std::nullopt;
        }

        diffieHellman = std::nullopt;
        pka = std::nullopt;
        ++pkaOperation;
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

        {
            infra::ProxyCreator<services::Aes128Ecb, void()> aes(aesCreator);
            aes->SetKey(reversedKey);
            aes->Encrypt(reversedInput, result);
        }

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
        infra::ProxyCreator<services::Aes128Ecb, void()> aes(aesCreator);
        AppendCmacBlocks(*aes, input);
    }

    // RFC 4493, section 2.4; the last block is held back from AppendCmac because it is combined with a subkey
    void BlePlatformWba::CalculateCmac(infra::ConstByteRange input, infra::ByteRange tag)
    {
        really_assert(tag.size() == blockSize);

        auto lastBlockSize = input.size() % blockSize;
        if (!input.empty() && lastBlockSize == 0)
            lastBlockSize = blockSize;

        infra::ProxyCreator<services::Aes128Ecb, void()> aes(aesCreator);
        AppendCmacBlocks(*aes, infra::DiscardTail(input, lastBlockSize));

        Block lastBlock{};
        std::copy(input.end() - lastBlockSize, input.end(), lastBlock.begin());

        auto subkey = DoubleCmacSubkey(EncryptCmacBlock(*aes, Block{}));
        if (lastBlockSize < blockSize)
        {
            lastBlock[lastBlockSize] = cmacPadding;
            subkey = DoubleCmacSubkey(subkey);
        }

        for (std::size_t index = 0; index != blockSize; ++index)
            lastBlock[index] ^= cmacState[index] ^ subkey[index];

        auto result = EncryptCmacBlock(*aes, lastBlock);
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

    bool BlePlatformWba::StartPublicKeyGeneration(infra::ConstByteRange key)
    {
        really_assert(key.size() == keySize);

        if (pka)
            return false;

        pka.emplace(pkaCreator);
        std::reverse_copy(key.begin(), key.end(), privateKey.begin());

        (*pka)->ScalarMultiplication(services::secp256r1, privateKey, {}, {}, [this](infra::ConstByteRange x, infra::ConstByteRange y)
            {
                infra::Copy(x, infra::Head(infra::MakeRange(publicKey), keySize));
                infra::Copy(y, infra::Tail(infra::MakeRange(publicKey), keySize));
                CompletePkaOperation();
            });

        return true;
    }

    void BlePlatformWba::ReadPublicKey(infra::ByteRange key) const
    {
        really_assert(key.size() == publicKey.size());

        std::reverse_copy(publicKey.begin(), publicKey.begin() + keySize, key.begin());
        std::reverse_copy(publicKey.begin() + keySize, publicKey.end(), key.begin() + keySize);
    }

    bool BlePlatformWba::StartDiffieHellmanKeyGeneration(infra::ConstByteRange key, infra::ConstByteRange peerPublicKey)
    {
        really_assert(key.size() == keySize && peerPublicKey.size() == publicKey.size());

        if (pka)
            return false;

        pka.emplace(pkaCreator);
        diffieHellman.emplace(**pka);
        diffieHellmanKeyValid = false;
        std::reverse_copy(key.begin(), key.end(), privateKey.begin());
        std::reverse_copy(peerPublicKey.begin(), peerPublicKey.begin() + keySize, publicKey.begin());
        std::reverse_copy(peerPublicKey.begin() + keySize, peerPublicKey.end(), publicKey.begin() + keySize);

        diffieHellman->CalculateSharedSecretKey(services::secp256r1, privateKey, publicKey, diffieHellmanKey, [this](bool valid)
            {
                diffieHellmanKeyValid = valid;
                CompletePkaOperation();
            });

        return true;
    }

    bool BlePlatformWba::ReadDiffieHellmanKey(infra::ByteRange key) const
    {
        really_assert(key.size() == diffieHellmanKey.size());

        if (!diffieHellmanKeyValid)
            return false;

        std::reverse_copy(diffieHellmanKey.begin(), diffieHellmanKey.end(), key.begin());
        return true;
    }

    void BlePlatformWba::AppendCmacBlocks(const services::Aes128Ecb& aes, infra::ConstByteRange input)
    {
        really_assert(input.size() % blockSize == 0);

        while (!input.empty())
        {
            Block block;
            std::transform(input.begin(), input.begin() + blockSize, cmacState.begin(), block.begin(), std::bit_xor<uint8_t>());
            cmacState = EncryptCmacBlock(aes, block);
            input = infra::DiscardHead(input, blockSize);
        }
    }

    BlePlatformWba::Block BlePlatformWba::EncryptCmacBlock(const services::Aes128Ecb& aes, const Block& input) const
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

    // The PKA reports from within its own callback, so it is released once that has returned
    void BlePlatformWba::CompletePkaOperation()
    {
        infra::EventDispatcher::Instance().Schedule([this, operation = pkaOperation]()
            {
                if (operation != pkaOperation)
                    return;

                diffieHellman = std::nullopt;
                pka = std::nullopt;

                BLEPLATCB_PkaComplete();
                BleStackCB_Process();
            });
    }
}

extern "C"
{
    void BLEPLAT_Init()
    {
        hal::BlePlatformWba::Instance().Reset();
    }

    // NVM and AES-CCM stay stubs; the basic stack does not use AES-CCM.
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

    int BLEPLAT_PkaStartP256Key(const uint32_t* local_private_key)
    {
        auto key = infra::ReinterpretCastByteRange(infra::MemoryRange<const uint32_t>(local_private_key, local_private_key + p256KeyWords));
        return hal::BlePlatformWba::Instance().StartPublicKeyGeneration(key) ? BLEPLAT_OK : BLEPLAT_BUSY;
    }

    void BLEPLAT_PkaReadP256Key(uint32_t* local_public_key)
    {
        hal::BlePlatformWba::Instance().ReadPublicKey(infra::ReinterpretCastByteRange(infra::MemoryRange<uint32_t>(local_public_key, local_public_key + 2 * p256KeyWords)));
    }

    int BLEPLAT_PkaStartDhKey(const uint32_t* local_private_key, const uint32_t* remote_public_key)
    {
        auto key = infra::ReinterpretCastByteRange(infra::MemoryRange<const uint32_t>(local_private_key, local_private_key + p256KeyWords));
        auto peerKey = infra::ReinterpretCastByteRange(infra::MemoryRange<const uint32_t>(remote_public_key, remote_public_key + 2 * p256KeyWords));
        return hal::BlePlatformWba::Instance().StartDiffieHellmanKeyGeneration(key, peerKey) ? BLEPLAT_OK : BLEPLAT_BUSY;
    }

    int BLEPLAT_PkaReadDhKey(uint32_t* dh_key)
    {
        return hal::BlePlatformWba::Instance().ReadDiffieHellmanKey(infra::ReinterpretCastByteRange(infra::MemoryRange<uint32_t>(dh_key, dh_key + p256KeyWords))) ? BLEPLAT_OK : BLEPLAT_EOF;
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
