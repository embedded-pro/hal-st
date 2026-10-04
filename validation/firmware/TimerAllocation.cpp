#include "validation/firmware/TimerAllocation.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace validation
{
    services::HilStatus TimerAllocation::Claim(uint8_t timer, services::HilOwner owner)
    {
        really_assert(timer < owners.size());

        if (owners[timer] && *owners[timer] != owner)
            return services::HilStatus::busy;

        owners[timer] = owner;
        return services::HilStatus::done;
    }

    void TimerAllocation::Release(uint8_t timer, services::HilOwner owner)
    {
        really_assert(timer < owners.size());

        if (owners[timer] == owner)
            owners[timer] = std::nullopt;
    }
}
