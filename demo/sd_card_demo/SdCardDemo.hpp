#pragma once

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace examples
{
    class SdCardDemo
    {
    public:
        static constexpr uint32_t scratchBlocks = 2;

        struct Config
        {
            constexpr Config()
            {}

            std::optional<uint32_t> firstScratchBlock;
        };

        SdCardDemo(hal::BlockDevice& device, infra::ByteRange buffers, const Config& config = Config());
        SdCardDemo(const SdCardDemo& other) = delete;
        SdCardDemo& operator=(const SdCardDemo& other) = delete;

        void Start(const infra::Function<void(bool passed)>& onDone);

    private:
        void ReadOriginal();
        void EraseScratch();
        void ReadErased();
        void WritePattern();
        void ReadPattern();
        void RestoreOriginal();
        void ReadRestored();
        void Finish();

        void Check(const char* step, hal::BlockDevice::Result result);
        void Verify(const char* step, infra::ConstByteRange expected);
        void FillPattern();

    private:
        hal::BlockDevice& device;
        infra::ByteRange buffers;
        Config config;
        infra::AutoResetFunction<void(bool passed)> onDone;
        uint32_t firstBlock{ 0 };
        infra::ByteRange original;
        infra::ByteRange pattern;
        infra::ByteRange readBack;
        bool passed{ true };
    };
}
