#ifndef HAL_ST_DIRECT_TEST_MODE_ST_HPP
#define HAL_ST_DIRECT_TEST_MODE_ST_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "services/ble/DirectTestMode.hpp"
#include <cstdint>

namespace hal
{
    class DirectTestModeSt
        : public services::DirectTestMode
    {
    public:
        // Implementation of services::DirectTestMode
        RequestStatus StartReceiverTest(uint8_t channel, Phy phy, const infra::Function<void(Result)>& onDone) override;
        RequestStatus StartTransmitterTest(uint8_t channel, uint8_t dataLength, PacketPayload payload, Phy phy, const infra::Function<void(Result)>& onDone) override;
        RequestStatus EndTest(const infra::Function<void(Result, uint16_t packetsReceived)>& onDone) override;
        RequestStatus StartUnmodulatedCarrier(uint8_t channel, uint8_t offset, const infra::Function<void(Result)>& onDone) override;
        RequestStatus StopUnmodulatedCarrier(const infra::Function<void(Result)>& onDone) override;
        RequestStatus SetTransmitPowerLevel(int8_t txPower, const infra::Function<void(Result)>& onDone) override;

    private:
        using Completion = infra::AutoResetFunction<void(Result)>;

        Completion onStartReceiverTestDone;
        Completion onStartTransmitterTestDone;
        Completion onStartUnmodulatedCarrierDone;
        Completion onStopUnmodulatedCarrierDone;
        Completion onSetTransmitPowerLevelDone;
        infra::AutoResetFunction<void(Result, uint16_t)> onEndTestDone;
    };
}

#endif
