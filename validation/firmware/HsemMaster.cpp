#include "validation/firmware/HsemMaster.hpp"

#if defined(STM32WB)

#include "infra/util/StaticStorage.hpp"

namespace validation
{
    hal::SynchronousHardwareSemaphoreMasterStm& HsemMaster()
    {
        // Never destroyed: the destructor gates the HSEM clock, which FlashCoordinatedWithWirelessStack and HAL_HSEM_* need
        static infra::StaticStorage<hal::SynchronousHardwareSemaphoreMasterStm> storage;
        static bool constructed = false;

        if (!constructed)
        {
            storage.Construct();
            constructed = true;
        }

        return *storage;
    }
}

#endif
