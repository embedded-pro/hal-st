#pragma once

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace examples
{
    class SdCardDemo
    {
    public:
        static constexpr uint32_t scratchBlocks = 16;

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
        using Result = hal::BlockDevice::Result;

        enum class Step : uint8_t
        {
            saveOriginal,
            writeSized,
            readSized,
            writeForErase,
            eraseRange,
            readErased,
            writeForPartialErase,
            erasePartial,
            readPartial,
            edgeCase,
            restoreOriginal,
            readRestored,
            done
        };

        static constexpr std::array<uint32_t, 4> sizes{ 1, 2, 5, 16 };
        static_assert(sizes.back() == scratchBlocks);

        void Run();
        void Completed(Result result);
        void Evaluate(Result result);
        void EvaluateSized(Result result);
        void EvaluateErased(Result result);
        void EvaluatePartial(Result result);
        void EvaluateEdgeCase(Result result);
        void GoTo(Step next);
        void Finish();

        void RunEdgeCase();
        void Read(infra::ByteRange range, uint32_t block);
        void Write(infra::ConstByteRange range, uint32_t block);
        void Erase(uint32_t begin, uint32_t end);

        infra::ByteRange Blocks(infra::ByteRange range, uint32_t count) const;
        infra::ByteRange Block(infra::ByteRange range, uint32_t blockIndex) const;
        void FillPattern(infra::ByteRange range, uint8_t seed) const;
        void Report(infra::BoundedConstString name, bool ok);

    private:
        hal::BlockDevice& device;
        infra::ByteRange buffers;
        Config config;
        infra::AutoResetFunction<void(bool passed)> onDone;
        Step step{ Step::saveOriginal };
        std::size_t index{ 0 };
        uint32_t blockSize{ 512 };
        uint32_t firstBlock{ 0 };
        infra::ByteRange original;
        infra::ByteRange pattern;
        infra::ByteRange readBack;
        uint32_t checks{ 0 };
        uint32_t failures{ 0 };
        uint32_t inlineCompletions{ 0 };
        bool inCall{ false };
    };
}
