#include "demo/sd_card_demo/SdCardDemo.hpp"
#include "infra/stream/StreamManipulators.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <algorithm>

namespace examples
{
    namespace
    {
        constexpr uint32_t bytesPerMebibyte = 1024 * 1024;

        struct EdgeCase
        {
            const char* name;
            hal::BlockDevice::Result expected;
        };

        constexpr std::array<EdgeCase, 8> edgeCases{ {
            { "empty read", hal::BlockDevice::Result::success },
            { "read last block", hal::BlockDevice::Result::success },
            { "read past the end", hal::BlockDevice::Result::outOfRange },
            { "read across the end", hal::BlockDevice::Result::outOfRange },
            { "write past the end", hal::BlockDevice::Result::outOfRange },
            { "erase across the end", hal::BlockDevice::Result::outOfRange },
            { "erase with end before begin", hal::BlockDevice::Result::outOfRange },
            { "empty erase", hal::BlockDevice::Result::success },
        } };

        const char* ResultName(hal::BlockDevice::Result result)
        {
            switch (result)
            {
                case hal::BlockDevice::Result::success:
                    return "success";
                case hal::BlockDevice::Result::notPresent:
                    return "not present";
                case hal::BlockDevice::Result::timeout:
                    return "timeout";
                case hal::BlockDevice::Result::crcError:
                    return "crc error";
                case hal::BlockDevice::Result::writeProtected:
                    return "write protected";
                case hal::BlockDevice::Result::outOfRange:
                    return "out of range";
                case hal::BlockDevice::Result::failed:
                    break;
            }

            return "failed";
        }

        bool Equal(infra::ConstByteRange expected, infra::ConstByteRange actual)
        {
            return expected.size() == actual.size() && std::equal(expected.begin(), expected.end(), actual.begin());
        }
    }

    SdCardDemo::SdCardDemo(hal::BlockDevice& device, infra::ByteRange buffers, const Config& config)
        : device{ device }
        , buffers{ buffers }
        , config{ config }
    {}

    void SdCardDemo::Start(const infra::Function<void(bool passed)>& onDone)
    {
        really_assert(!this->onDone);
        this->onDone = onDone;

        if (device.NumberOfBlocks() == 0)
        {
            services::GlobalTracer().Trace() << "SD card: not present";
            this->onDone(false);
            return;
        }

        blockSize = device.BlockSize();
        const auto scratchBytes = scratchBlocks * blockSize;
        really_assert(buffers.size() >= 3 * scratchBytes);
        really_assert(device.NumberOfBlocks() >= scratchBlocks);

        firstBlock = config.firstScratchBlock.value_or(device.NumberOfBlocks() - scratchBlocks);
        really_assert(firstBlock <= device.NumberOfBlocks() - scratchBlocks);

        original = infra::Head(buffers, scratchBytes);
        pattern = infra::Head(infra::DiscardHead(buffers, scratchBytes), scratchBytes);
        readBack = infra::Head(infra::DiscardHead(buffers, 2 * scratchBytes), scratchBytes);

        checks = 0;
        failures = 0;
        inlineCompletions = 0;
        index = 0;

        services::GlobalTracer().Trace() << "SD card: " << device.NumberOfBlocks() << " blocks of " << blockSize << " bytes, " << static_cast<uint32_t>(uint64_t{ device.NumberOfBlocks() } * blockSize / bytesPerMebibyte) << " MiB";
        services::GlobalTracer().Trace() << "SD card: scratch blocks " << firstBlock << " to " << firstBlock + scratchBlocks - 1;

        GoTo(Step::saveOriginal);
    }

    void SdCardDemo::Run()
    {
        switch (step)
        {
            case Step::saveOriginal:
                Read(original, firstBlock);
                break;
            case Step::writeSized:
                FillPattern(Blocks(pattern, sizes[index]), static_cast<uint8_t>(sizes[index]));
                Write(Blocks(pattern, sizes[index]), firstBlock);
                break;
            case Step::readSized:
                Read(Blocks(readBack, sizes[index]), firstBlock);
                break;
            case Step::writeForErase:
                FillPattern(Blocks(pattern, 2), 0x5a);
                Write(Blocks(pattern, 2), firstBlock);
                break;
            case Step::eraseRange:
                Erase(firstBlock, firstBlock + 2);
                break;
            case Step::readErased:
                Read(Blocks(readBack, 2), firstBlock);
                break;
            case Step::writeForPartialErase:
                FillPattern(Blocks(pattern, 3), 0xa5);
                Write(Blocks(pattern, 3), firstBlock);
                break;
            case Step::erasePartial:
                Erase(firstBlock + 1, firstBlock + 2);
                break;
            case Step::readPartial:
                Read(Blocks(readBack, 3), firstBlock);
                break;
            case Step::edgeCase:
                RunEdgeCase();
                break;
            case Step::restoreOriginal:
                Write(original, firstBlock);
                break;
            case Step::readRestored:
                Read(readBack, firstBlock);
                break;
            case Step::done:
                Finish();
                break;
        }
    }

    void SdCardDemo::RunEdgeCase()
    {
        const uint32_t end = device.NumberOfBlocks();

        switch (index)
        {
            case 0:
                Read(Blocks(readBack, 0), firstBlock);
                break;
            case 1:
                Read(Blocks(readBack, 1), end - 1);
                break;
            case 2:
                Read(Blocks(readBack, 1), end);
                break;
            case 3:
                Read(Blocks(readBack, 2), end - 1);
                break;
            case 4:
                Write(Blocks(pattern, 1), end);
                break;
            case 5:
                Erase(end - 1, end + 1);
                break;
            case 6:
                Erase(firstBlock + 2, firstBlock + 1);
                break;
            default:
                Erase(firstBlock, firstBlock);
                break;
        }
    }

    void SdCardDemo::Completed(Result result)
    {
        if (inCall)
            ++inlineCompletions;

        if (result != Result::success && step != Step::edgeCase)
            services::GlobalTracer().Trace() << "SD card: operation ended with " << ResultName(result);

        Evaluate(result);
    }

    void SdCardDemo::Evaluate(Result result)
    {
        const bool ok = result == Result::success;

        switch (step)
        {
            case Step::saveOriginal:
                Report("save original", ok);
                GoTo(ok ? Step::writeSized : Step::done);
                break;
            case Step::writeSized:
            case Step::readSized:
                EvaluateSized(result);
                break;
            case Step::writeForErase:
                Report("write before erase", ok);
                GoTo(Step::eraseRange);
                break;
            case Step::eraseRange:
                Report("erase 2 blocks", ok);
                GoTo(Step::readErased);
                break;
            case Step::readErased:
                EvaluateErased(result);
                break;
            case Step::writeForPartialErase:
                Report("write 3 blocks before partial erase", ok);
                GoTo(Step::erasePartial);
                break;
            case Step::erasePartial:
                Report("erase 1 block", ok);
                GoTo(Step::readPartial);
                break;
            case Step::readPartial:
                EvaluatePartial(result);
                break;
            case Step::edgeCase:
                EvaluateEdgeCase(result);
                break;
            case Step::restoreOriginal:
                Report("restore original", ok);
                GoTo(Step::readRestored);
                break;
            case Step::readRestored:
                Report("restore verified", ok && Equal(original, readBack));
                GoTo(Step::done);
                break;
            case Step::done:
                break;
        }
    }

    void SdCardDemo::EvaluateSized(Result result)
    {
        const uint32_t blocks = sizes[index];
        infra::StringOutputStream::WithStorage<32> name;

        if (step == Step::writeSized)
        {
            name << "write " << blocks << (blocks == 1 ? " block" : " blocks");
            Report(name.Storage(), result == Result::success);
            GoTo(Step::readSized);
            return;
        }

        name << "read back " << blocks << (blocks == 1 ? " block" : " blocks");
        Report(name.Storage(), result == Result::success && Equal(Blocks(pattern, blocks), Blocks(readBack, blocks)));

        if (++index != sizes.size())
            GoTo(Step::writeSized);
        else
        {
            index = 0;
            GoTo(Step::writeForErase);
        }
    }

    void SdCardDemo::EvaluateErased(Result result)
    {
        const auto erased = Blocks(readBack, 2);

        Report("erase changed the data", result == Result::success && !Equal(Blocks(pattern, 2), erased));

        if (result == Result::success)
        {
            const auto first = erased.front();
            const bool uniform = std::all_of(erased.begin(), erased.end(), [first](uint8_t value)
                {
                    return value == first;
                });
            services::GlobalTracer().Trace() << "SD card: erased blocks read as " << (uniform ? "uniform 0x" : "mixed, first byte 0x") << infra::hex << static_cast<uint32_t>(first);
        }

        GoTo(Step::writeForPartialErase);
    }

    void SdCardDemo::EvaluatePartial(Result result)
    {
        const bool ok = result == Result::success;

        Report("erase kept the block before its range", ok && Equal(Block(pattern, 0), Block(readBack, 0)));
        Report("erase cleared the block in its range", ok && !Equal(Block(pattern, 1), Block(readBack, 1)));
        Report("erase kept the block after its range", ok && Equal(Block(pattern, 2), Block(readBack, 2)));

        index = 0;
        GoTo(Step::edgeCase);
    }

    void SdCardDemo::EvaluateEdgeCase(Result result)
    {
        const bool ok = result == edgeCases[index].expected;
        Report(edgeCases[index].name, ok);

        if (!ok)
            services::GlobalTracer().Trace() << "SD card: expected " << ResultName(edgeCases[index].expected) << ", got " << ResultName(result);

        if (++index != edgeCases.size())
            GoTo(Step::edgeCase);
        else
            GoTo(Step::restoreOriginal);
    }

    void SdCardDemo::GoTo(Step next)
    {
        step = next;
        Run();
    }

    void SdCardDemo::Finish()
    {
        Report("no completion ran inside its call", inlineCompletions == 0);

        services::GlobalTracer().Trace() << "SD card validation: " << checks - failures << "/" << checks << " checks passed";
        services::GlobalTracer().Trace() << "SD card demo: " << (failures == 0 ? "PASSED" : "FAILED");
        onDone(failures == 0);
    }

    void SdCardDemo::Read(infra::ByteRange range, uint32_t block)
    {
        inCall = true;
        device.ReadBlocks(range, block, [this](Result result)
            {
                Completed(result);
            });
        inCall = false;
    }

    void SdCardDemo::Write(infra::ConstByteRange range, uint32_t block)
    {
        inCall = true;
        device.WriteBlocks(range, block, [this](Result result)
            {
                Completed(result);
            });
        inCall = false;
    }

    void SdCardDemo::Erase(uint32_t begin, uint32_t end)
    {
        inCall = true;
        device.EraseBlocks(begin, end, [this](Result result)
            {
                Completed(result);
            });
        inCall = false;
    }

    infra::ByteRange SdCardDemo::Blocks(infra::ByteRange range, uint32_t count) const
    {
        return infra::Head(range, count * blockSize);
    }

    infra::ByteRange SdCardDemo::Block(infra::ByteRange range, uint32_t blockIndex) const
    {
        return infra::Head(infra::DiscardHead(range, blockIndex * blockSize), blockSize);
    }

    void SdCardDemo::FillPattern(infra::ByteRange range, uint8_t seed) const
    {
        uint32_t state = seed;

        for (auto& byte : range)
        {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<uint8_t>(state >> 24);
        }
    }

    void SdCardDemo::Report(infra::BoundedConstString name, bool ok)
    {
        ++checks;

        if (!ok)
            ++failures;

        services::GlobalTracer().Trace() << "SD card: " << (ok ? "PASS " : "FAIL ") << name;
    }
}
