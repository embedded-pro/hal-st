#pragma once

#include "hal_st/middlewares/ble_middleware/HciEventObserver.hpp"
#include "infra/util/Function.hpp"
#include "services/ble/BondStorageSynchronizer.hpp"
#include "services/hil/HilResponse.hpp"

namespace validation
{
    class BleController
    {
    public:
        using OnReady = infra::Function<void(hal::HciEventSource&, services::BondStorageSynchronizer&)>;

        void Start(const OnReady& onReady);

        void PrintStack(services::HilResponse::Line& line) const;

    private:
        OnReady onReady;
    };
}
