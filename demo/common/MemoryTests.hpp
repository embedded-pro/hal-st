#pragma once

#include "infra/util/MemoryRange.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace main_
{
    namespace detail
    {
        template<class Word>
        Word PatternOf(std::size_t index, uint32_t complement)
        {
            constexpr uint32_t addressSpread = 0x9e3779b1;

            return static_cast<Word>((static_cast<uint32_t>(index) * addressSpread) ^ complement);
        }
    }

    // A walking one on the data bus: a stuck or shorted data line shows up as a different value read back
    template<class Word>
    std::size_t TestDataBus(volatile Word* memory)
    {
        std::size_t errors = 0;

        for (Word pattern = 1; pattern != 0; pattern = static_cast<Word>(pattern << 1))
        {
            memory[0] = pattern;

            if (memory[0] != pattern)
                ++errors;
        }

        return errors;
    }

    // One write per address line to a power of two offset, so an address line that is stuck or shorted to another aliases two of them; words must be a power of two
    template<class Word>
    std::size_t TestAddressBus(volatile Word* memory, std::size_t words)
    {
        const Word pattern = static_cast<Word>(0xaaaaaaaa);
        const Word antipattern = static_cast<Word>(~pattern);
        std::size_t errors = 0;

        for (std::size_t offset = 1; offset < words; offset <<= 1)
            memory[offset] = pattern;

        memory[0] = antipattern;

        for (std::size_t offset = 1; offset < words; offset <<= 1)
            if (memory[offset] != pattern)
                ++errors;

        for (std::size_t testOffset = 1; testOffset < words; testOffset <<= 1)
        {
            memory[testOffset] = antipattern;

            if (memory[0] != antipattern)
                ++errors;

            for (std::size_t offset = 1; offset < words; offset <<= 1)
                if (memory[offset] != pattern && offset != testOffset)
                    ++errors;

            memory[testOffset] = pattern;
        }

        return errors;
    }

    // A different value per address and its complement, so a stuck cell, a shorted address line or a missed refresh all show up
    template<class Word>
    std::size_t TestPattern(volatile Word* memory, std::size_t words)
    {
        std::size_t errors = 0;

        for (uint32_t complement : { 0u, ~0u })
        {
            for (std::size_t index = 0; index != words; ++index)
                memory[index] = detail::PatternOf<Word>(index, complement);

            for (std::size_t index = 0; index != words; ++index)
                if (memory[index] != detail::PatternOf<Word>(index, complement))
                    ++errors;
        }

        return errors;
    }

    template<class Word>
    std::size_t TestMemory(infra::ByteRange memory, std::size_t patternBytes)
    {
        auto* words = reinterpret_cast<volatile Word*>(memory.begin());
        const std::size_t wordCount = memory.size() / sizeof(Word);

        return TestDataBus(words) + TestAddressBus(words, wordCount) + TestPattern(words, std::min(wordCount, patternBytes / sizeof(Word)));
    }
}
