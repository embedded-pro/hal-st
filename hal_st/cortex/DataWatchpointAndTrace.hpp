#pragma once

#include DEVICE_HEADER
#include "infra/util/InterfaceConnector.hpp"

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
