#include "validation/firmware/ResourceAllocation.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace validation
{
    services::HilStatus ResourceAllocation::Claim(Resource resource, uint8_t index, services::HilOwner owner)
    {
        auto& slot = Slot(resource, index);

        if (slot && *slot != owner)
            return services::HilStatus::busy;

        slot = owner;
        return services::HilStatus::done;
    }

    void ResourceAllocation::Release(Resource resource, uint8_t index, services::HilOwner owner)
    {
        auto& slot = Slot(resource, index);

        if (slot == owner)
            slot = std::nullopt;
    }

    std::optional<services::HilOwner>& ResourceAllocation::Slot(Resource resource, uint8_t index)
    {
        auto row = static_cast<std::size_t>(resource);
        really_assert(row < owners.size() && index < indices);

        return owners[row][index];
    }
}
