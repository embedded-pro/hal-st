#include "demo/sd_card_demo/SdCardDemo.hpp"
#include "infra/stream/StreamManipulators.hpp"
#include "infra/util/MemoryRange.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include <algorithm>

namespace examples
{
    namespace
    {
        constexpr uint32_t bytesPerMebibyte = 1024 * 1024;

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
        passed = true;

        if (device.NumberOfBlocks() == 0)
        {
            services::GlobalTracer().Trace() << "SD card: not present";
            passed = false;
            Finish();
            return;
        }

        auto scratchBytes = scratchBlocks * device.BlockSize();
        really_assert(buffers.size() >= 3 * scratchBytes);
        really_assert(device.NumberOfBlocks() >= scratchBlocks);

        firstBlock = config.firstScratchBlock.value_or(device.NumberOfBlocks() - scratchBlocks);
        really_assert(firstBlock <= device.NumberOfBlocks() - scratchBlocks);

        original = infra::Head(buffers, scratchBytes);
        pattern = infra::Head(infra::DiscardHead(buffers, scratchBytes), scratchBytes);
        readBack = infra::Head(infra::DiscardHead(buffers, 2 * scratchBytes), scratchBytes);

        services::GlobalTracer().Trace() << "SD card: " << device.NumberOfBlocks() << " blocks of " << device.BlockSize() << " bytes, " << static_cast<uint32_t>(uint64_t{ device.NumberOfBlocks() } * device.BlockSize() / bytesPerMebibyte) << " MiB";
        services::GlobalTracer().Trace() << "SD card: scratch blocks " << firstBlock << " to " << firstBlock + scratchBlocks - 1;

        ReadOriginal();
    }

    void SdCardDemo::ReadOriginal()
    {
        device.ReadBlocks(original, firstBlock, [this](hal::BlockDevice::Result result)
            {
                Check("read original", result);

                if (result == hal::BlockDevice::Result::success)
                    EraseScratch();
                else
                    Finish();
            });
    }

    void SdCardDemo::EraseScratch()
    {
        device.EraseBlocks(firstBlock, firstBlock + scratchBlocks, [this](hal::BlockDevice::Result result)
            {
                Check("erase", result);
                ReadErased();
            });
    }

    void SdCardDemo::ReadErased()
    {
        device.ReadBlocks(readBack, firstBlock, [this](hal::BlockDevice::Result result)
            {
                Check("read erased", result);

                if (result == hal::BlockDevice::Result::success)
                {
                    auto first = readBack.front();
                    auto uniform = std::all_of(readBack.begin(), readBack.end(), [first](uint8_t value)
                        {
                            return value == first;
                        });
                    services::GlobalTracer().Trace() << "SD card: erased blocks read as " << (uniform ? "uniform 0x" : "mixed, first byte 0x") << infra::hex << static_cast<uint32_t>(first);
                }

                WritePattern();
            });
    }

    void SdCardDemo::WritePattern()
    {
        FillPattern();
        device.WriteBlocks(pattern, firstBlock, [this](hal::BlockDevice::Result result)
            {
                Check("write pattern", result);
                ReadPattern();
            });
    }

    void SdCardDemo::ReadPattern()
    {
        device.ReadBlocks(readBack, firstBlock, [this](hal::BlockDevice::Result result)
            {
                Check("read pattern", result);

                if (result == hal::BlockDevice::Result::success)
                    Verify("pattern compare", pattern);

                RestoreOriginal();
            });
    }

    void SdCardDemo::RestoreOriginal()
    {
        device.WriteBlocks(original, firstBlock, [this](hal::BlockDevice::Result result)
            {
                Check("restore original", result);
                ReadRestored();
            });
    }

    void SdCardDemo::ReadRestored()
    {
        device.ReadBlocks(readBack, firstBlock, [this](hal::BlockDevice::Result result)
            {
                Check("read restored", result);

                if (result == hal::BlockDevice::Result::success)
                    Verify("restore compare", original);

                Finish();
            });
    }

    void SdCardDemo::Finish()
    {
        services::GlobalTracer().Trace() << "SD card demo: " << (passed ? "PASSED" : "FAILED");
        onDone(passed);
    }

    void SdCardDemo::Check(const char* step, hal::BlockDevice::Result result)
    {
        if (result == hal::BlockDevice::Result::success)
            services::GlobalTracer().Trace() << "SD card: PASS " << step;
        else
        {
            services::GlobalTracer().Trace() << "SD card: FAIL " << step << ": " << ResultName(result);
            passed = false;
        }
    }

    void SdCardDemo::Verify(const char* step, infra::ConstByteRange expected)
    {
        auto equal = std::equal(expected.begin(), expected.end(), readBack.begin());
        services::GlobalTracer().Trace() << "SD card: " << (equal ? "PASS " : "FAIL ") << step;
        passed = passed && equal;
    }

    void SdCardDemo::FillPattern()
    {
        uint8_t value = static_cast<uint8_t>(firstBlock);

        for (auto& byte : pattern)
        {
            byte = value;
            value = static_cast<uint8_t>(value * 5 + 1);
        }
    }
}
