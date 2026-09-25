#pragma once

#include "hal_st/middlewares/ble_middleware/SystemTransportLayerWba.hpp"
#include "infra/util/WithStorage.hpp"
#include "services/tracer/Tracer.hpp"

namespace hal
{
    class TracingSystemTransportLayerWba
        : public SystemTransportLayerWba
    {
    public:
        template<uint8_t NumberOfLinks>
        using WithLinks = infra::WithStorage<TracingSystemTransportLayerWba, Storage<NumberOfLinks>>;

        template<uint8_t NumberOfLinks>
        TracingSystemTransportLayerWba(Storage<NumberOfLinks>& storage, services::ConfigurationStoreAccess<infra::ByteRange> bondBlob, const HardwareDependencies& hardware, const Config& config, services::Tracer& tracer)
            : SystemTransportLayerWba(storage, bondBlob, hardware, config)
            , tracer(tracer)
        {
            TraceVersion();
        }

    protected:
        void EventFlowPaused() override;
        void EventFlowResumed() override;

    private:
        void TraceVersion() const;

    private:
        services::Tracer& tracer;
    };
}
