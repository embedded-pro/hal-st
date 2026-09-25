#pragma once

#include "infra/timer/Timer.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/ProxyCreator.hpp"
#include "services/crypto/Aes.hpp"
#include "services/crypto/EllipticCurve.hpp"
#include "services/crypto/EllipticCurveDiffieHellman.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace hal
{
    class BlePlatformWba
        : public infra::InterfaceConnector<BlePlatformWba>
    {
    public:
        struct TimerSlot
        {
            infra::TimerSingleShot timer;
            std::optional<uint16_t> id;
        };

        // The stack runs timers for advertising and GAP procedures, and per link for GATT, L2CAP and SMP
        static constexpr std::size_t TimersForLinks(std::size_t numberOfLinks)
        {
            return 2 + 4 * numberOfLinks;
        }

        // The AES and PKA are only held while an operation runs, so that the application can use them in between
        using AesCreator = infra::CreatorBase<services::Aes128Ecb, void()>;
        using PkaCreator = infra::CreatorBase<services::EllipticCurveOperations, void()>;

        BlePlatformWba(infra::MemoryRange<TimerSlot> timers, AesCreator& aesCreator, PkaCreator& pkaCreator);

        void Reset();

        void EncryptBlock(infra::ConstByteRange key, infra::ConstByteRange input, infra::ByteRange output);
        void SetCmacKey(infra::ConstByteRange key);
        void AppendCmac(infra::ConstByteRange input);
        void CalculateCmac(infra::ConstByteRange input, infra::ByteRange tag);

        bool StartTimer(uint16_t id, uint32_t timeoutMs);
        void StopTimer(uint16_t id);

        // Keys are P-256 integers of 32 bytes, least significant byte first; a public key is x followed by y
        bool StartPublicKeyGeneration(infra::ConstByteRange privateKey);
        void ReadPublicKey(infra::ByteRange publicKey) const;
        bool StartDiffieHellmanKeyGeneration(infra::ConstByteRange privateKey, infra::ConstByteRange peerPublicKey);
        bool ReadDiffieHellmanKey(infra::ByteRange key) const;

    private:
        static constexpr std::size_t blockSize = 16;
        using Block = std::array<uint8_t, blockSize>;

        static constexpr std::size_t keySize = 32;

        void AppendCmacBlocks(const services::Aes128Ecb& aes, infra::ConstByteRange input);
        Block EncryptCmacBlock(const services::Aes128Ecb& aes, const Block& input) const;
        void ExpireTimer(TimerSlot& slot);
        void CompletePkaOperation();

    private:
        infra::MemoryRange<TimerSlot> timers;
        AesCreator& aesCreator;
        PkaCreator& pkaCreator;

        Block cmacKey{};
        Block cmacState{};

        std::optional<infra::ProxyCreator<services::EllipticCurveOperations, void()>> pka;
        std::optional<services::EllipticCurveDiffieHellman> diffieHellman;
        uint32_t pkaOperation = 0;
        bool diffieHellmanKeyValid = false;
        std::array<uint8_t, keySize> privateKey{};
        std::array<uint8_t, 2 * keySize> publicKey{};
        std::array<uint8_t, keySize> diffieHellmanKey{};
    };
}
