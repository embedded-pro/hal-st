#pragma once

#include "hal_st/stm32fxxx/PkaStm.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousAesStm.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include "infra/util/MemoryRange.hpp"
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

        explicit BlePlatformWba(infra::MemoryRange<TimerSlot> timers);

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

        Block EncryptCmacBlock(const Block& input);
        void ExpireTimer(TimerSlot& slot);
        void CompletePkaOperation();

    private:
        infra::MemoryRange<TimerSlot> timers;
        SynchronousAes128EcbStm aes{ SynchronousAes128EcbStm::Config() };
        Block cmacKey{};
        Block cmacState{};

        PkaStm pka;
        services::EllipticCurveDiffieHellman diffieHellman{ pka };
        bool pkaBusy = false;
        bool diffieHellmanKeyValid = false;
        std::array<uint8_t, keySize> privateKey{};
        std::array<uint8_t, 2 * keySize> publicKey{};
        std::array<uint8_t, keySize> diffieHellmanKey{};
    };
}
