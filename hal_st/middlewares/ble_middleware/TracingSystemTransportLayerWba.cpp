#include "hal_st/middlewares/ble_middleware/TracingSystemTransportLayerWba.hpp"

namespace hal
{
    void TracingSystemTransportLayerWba::EventFlowPaused()
    {
        tracer.Trace() << "SystemTransportLayerWba: HCI event queue full, event flow paused";
        SystemTransportLayerWba::EventFlowPaused();
    }

    void TracingSystemTransportLayerWba::EventFlowResumed()
    {
        tracer.Trace() << "SystemTransportLayerWba: HCI event flow resumed";
        SystemTransportLayerWba::EventFlowResumed();
    }

    void TracingSystemTransportLayerWba::TraceVersion() const
    {
        const auto version = GetVersion();

        tracer.Trace() << "BLE stack firmware build: " << version.firmwareBuildNumber;
        tracer.Trace() << "HCI version: " << version.hciVersion << ", subversion: 0x" << infra::hex << version.hciSubversion;
        tracer.Trace() << "LMP version: " << version.lmpVersion << ", subversion: 0x" << infra::hex << version.lmpSubversion;
        tracer.Trace() << "Company identifier: 0x" << infra::hex << version.companyIdentifier;
    }
}
