#include "demo/common/TraceI2cScan.hpp"
#include "services/tracer/GlobalTracer.hpp"

namespace main_
{
    void TraceI2cScan(services::I2cScanner& scanner)
    {
        if (scanner.Scanning())
        {
            services::GlobalTracer().Trace() << "i2c scan in progress";
            return;
        }

        scanner.Scan([](hal::I2cAddress address)
            {
                services::GlobalTracer().Trace() << "i2c 0x" << infra::hex << static_cast<uint32_t>(address.address) << " acknowledged";
            },
            [](uint32_t numberOfDevices)
            {
                services::GlobalTracer().Trace() << "i2c scan done, " << numberOfDevices << " devices";
            });
    }
}
