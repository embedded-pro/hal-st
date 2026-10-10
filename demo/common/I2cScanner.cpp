#include "demo/common/I2cScanner.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/tracer/GlobalTracer.hpp"

namespace main_
{
    I2cScanner::I2cScanner(hal::I2cMaster& i2c)
        : i2c(i2c)
    {}

    void I2cScanner::Scan()
    {
        if (scanning)
        {
            services::GlobalTracer().Trace() << "i2c scan in progress";
            return;
        }

        scanning = true;
        found = 0;
        address = firstAddress;
        Probe();
    }

    void I2cScanner::Probe()
    {
        i2c.ReceiveData(hal::I2cAddress(address), infra::MakeRange(data), hal::Action::stop, [this](hal::Result result)
            {
                if (result == hal::Result::complete)
                {
                    ++found;
                    services::GlobalTracer().Trace() << "i2c 0x" << infra::hex << static_cast<uint32_t>(address) << " acknowledged";
                }

                if (address++ != lastAddress)
                    Probe();
                else
                {
                    scanning = false;
                    services::GlobalTracer().Trace() << "i2c scan done, " << found << " devices";
                }
            });
    }
}
