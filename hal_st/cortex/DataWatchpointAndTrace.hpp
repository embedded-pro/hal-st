#pragma once

#include DEVICE_HEADER
#include "infra/util/InterfaceConnector.hpp"

#if defined(__CORTEX_M) && (__CORTEX_M == 4)

namespace hal
{
    class DataWatchPointAndTrace
        : public infra::InterfaceConnector<DataWatchPointAndTrace>
    {
    public:
        DataWatchPointAndTrace();
        ~DataWatchPointAndTrace();

        void Start() const;
        uint32_t Stop() const;
    };
}

#endif
