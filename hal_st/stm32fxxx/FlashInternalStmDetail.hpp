#ifndef HAL_FLASH_INTERNAL_STM_DETAIL_HPP
#define HAL_FLASH_INTERNAL_STM_DETAIL_HPP

#include DEVICE_HEADER
#include "infra/util/ReallyAssert.hpp"
#include "services/flash/FlashAlign.hpp"
#include <algorithm>
#include <cstdint>

namespace hal
{
    namespace detail
    {
        template<typename alignment>
        void AlignedWriteBufferByAddress(services::FlashAlign::Chunk& chunk, uint32_t& fullAddress, uint32_t flashType)
        {
            auto range = infra::ReinterpretCastMemoryRange<const alignment>(chunk.data);
            for (const auto& data : range)
            {
                auto result = HAL_FLASH_Program(flashType, fullAddress, reinterpret_cast<uint32_t>(&data));
                really_assert(result == HAL_OK);
                fullAddress += sizeof(alignment);
            }
        }

        template<typename alignment>
        void AlignedWriteBufferByValue(services::FlashAlign::Chunk& chunk, uint32_t& fullAddress, uint32_t flashType)
        {
            auto range = infra::ReinterpretCastMemoryRange<const alignment>(chunk.data);
            for (alignment data : range)
            {
                auto result = HAL_FLASH_Program(flashType, fullAddress, data);
                really_assert(result == HAL_OK);
                fullAddress += sizeof(alignment);
            }
        }

        template<typename alignment, uint32_t flashType, bool byAddress>
        void AlignedWriteBuffer(infra::ConstByteRange buffer, uint32_t address, uint32_t flashMemoryBegin)
        {
            services::FlashAlign::WithAlignment<sizeof(alignment)> flashAlign;
            flashAlign.Align(address, buffer);

            services::FlashAlign::Chunk* chunk = flashAlign.First();
            while (chunk != nullptr)
            {
                really_assert(chunk->data.size() % sizeof(alignment) == 0);
                auto fullAddress = flashMemoryBegin + chunk->alignedAddress;

                if constexpr (byAddress)
                    detail::AlignedWriteBufferByAddress<alignment>(*chunk, fullAddress, flashType);
                else
                    detail::AlignedWriteBufferByValue<alignment>(*chunk, fullAddress, flashType);

                chunk = flashAlign.Next();
            }
        }

#if defined(STM32WB) || defined(STM32WBA)
        inline void ErasePages(uint32_t page, uint32_t endPage)
        {
#if defined(FLASH_DBANK_SUPPORT)
            const uint32_t pagesPerBank = FLASH_PAGE_NB;
            const bool swapped = READ_BIT(FLASH->OPTR, FLASH_OPTR_SWAP_BANK) != 0;
#endif

            while (page != endPage)
            {
                FLASH_EraseInitTypeDef eraseInitStruct{};
                eraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
#if defined(FLASH_DBANK_SUPPORT)
                const uint32_t bankEndPage = std::min(endPage, (page / pagesPerBank + 1) * pagesPerBank);
                eraseInitStruct.Banks = (page >= pagesPerBank) == swapped ? FLASH_BANK_1 : FLASH_BANK_2;
                eraseInitStruct.Page = page % pagesPerBank;
#else
                const uint32_t bankEndPage = endPage;
                eraseInitStruct.Page = page;
#endif
                eraseInitStruct.NbPages = bankEndPage - page;

                uint32_t pageError = 0;
                auto result = HAL_FLASHEx_Erase(&eraseInitStruct, &pageError);
                really_assert(result == HAL_OK);
                page = bankEndPage;
            }
        }
#endif
    }
}

#endif
