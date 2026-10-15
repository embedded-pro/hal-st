#pragma once

#include "hal_st/middlewares/ble_middleware/HciEventObserver.hpp"
#include "infra/util/Function.hpp"
#include "services/ble/BondStorageSynchronizer.hpp"
#include "services/hil/HilResponse.hpp"

namespace validation
{
    // The BLE stack of the board: on NUCLEO-WB55RG the transport to the wireless coprocessor, on NUCLEO-WBA55CG the
    // host stack and link layer on the application core. Nothing starts before `Start`, so a board without the
    // coprocessor's stack still answers `ping`; the volatile bond storage lives as long as the stack.
    class BleController
    {
    public:
        using OnReady = infra::Function<void(hal::HciEventSource&, services::BondStorageSynchronizer&)>;

        // Starts the stack once per boot. `onReady` is called when the controller can take GAP and GATT objects: on
        // WB55 from the event loop after the coprocessor reports ready (a second or so), on WBA55 before this returns.
        void Start(const OnReady& onReady);

        // ` stack=<version>`, or ` stack=-` before `Start`.
        void PrintStack(services::HilResponse::Line& line) const;

    private:
        OnReady onReady;
    };
}
