#include <algorithm>
#include <cstdint>

extern "C"
{
#include "bleplat.h"
}

namespace
{
    constexpr std::size_t aesBlockSize = 16;
    constexpr std::size_t p256PublicKeyWords = 16;
    constexpr std::size_t p256DhKeyWords = 8;
}

// Stubs so the stack links; each subsystem gets its real implementation in a later step.
extern "C"
{
    void BLEPLAT_Init()
    {}

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

    void BLEPLAT_AesEcbEncrypt(const uint8_t*, const uint8_t*, uint8_t* output)
    {
        std::fill_n(output, aesBlockSize, 0);
    }

    void BLEPLAT_AesCmacSetKey(const uint8_t*)
    {}

    void BLEPLAT_AesCmacCompute(const uint8_t*, uint32_t, uint8_t* output_tag)
    {
        if (output_tag != nullptr)
            std::fill_n(output_tag, aesBlockSize, 0);
    }

    int BLEPLAT_AesCcmCrypt(uint8_t, const uint8_t*, uint8_t, const uint8_t*, uint16_t, const uint8_t*, uint32_t, const uint8_t*, uint8_t, uint8_t*, uint8_t*)
    {
        return BLEPLAT_ERROR;
    }

    void BLEPLAT_RngGet(uint8_t n, uint32_t* val)
    {
        std::fill_n(val, n, 0);
    }

    uint8_t BLEPLAT_TimerStart(uint16_t, uint32_t)
    {
        return static_cast<uint8_t>(BLEPLAT_ERROR);
    }

    void BLEPLAT_TimerStop(uint16_t)
    {}
}
