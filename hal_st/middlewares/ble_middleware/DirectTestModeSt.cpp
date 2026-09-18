#include "hal_st/middlewares/ble_middleware/DirectTestModeSt.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/EnumCast.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>

extern "C"
{
#include "ble_hal_aci.h"
#include "ble_hci_le.h"
}

namespace
{
    // The receiver and transmitter tests take a modulation index of standard or stable; the
    // specification's own default is standard.
    // Bluetooth Core Specification, Volume 4, Part E, section 7.8.28
    constexpr uint8_t standardModulationIndex = 0x00u;

    services::DirectTestMode::Result ResultOf(tBleStatus status)
    {
        if (status == BLE_STATUS_SUCCESS)
            return services::DirectTestMode::Result::success;
        if (status == BLE_STATUS_INVALID_PARAMS)
            return services::DirectTestMode::Result::invalidParameter;

        return services::DirectTestMode::Result::controllerError;
    }

    template<class Completion, class... Results>
    void Complete(Completion& completion, Results... results)
    {
        infra::EventDispatcher::Instance().Schedule([&completion, results...]()
            {
                if (completion)
                    completion(results...);
            });
    }

#if defined(STM32WB)
    // PA_Level to output power, in tenths of a dBm, as ACI_HAL_SET_TX_POWER_LEVEL documents it.
    // The values are indicative and depend on the device and its package, so a request is answered
    // with the level whose documented power is nearest.
    constexpr std::array<int16_t, 32> paLevelPower{ -400, -209, -198, -189, -176, -165, -153, -141,
        -132, -121, -109, -99, -89, -78, -69, -59,
        -50, -40, -32, -25, -18, -13, -9, -5,
        -2, 0, 10, 20, 30, 40, 50, 60 };

    uint8_t NearestPaLevel(int8_t txPower)
    {
        auto wanted = static_cast<int16_t>(txPower * 10);
        auto nearest = std::min_element(paLevelPower.begin(), paLevelPower.end(), [wanted](auto left, auto right)
            {
                return std::abs(left - wanted) < std::abs(right - wanted);
            });

        return static_cast<uint8_t>(std::distance(paLevelPower.begin(), nearest));
    }
#endif
}

namespace hal
{
    services::DirectTestMode::RequestStatus DirectTestModeSt::StartReceiverTest(uint8_t channel, Phy phy, const infra::Function<void(Result)>& onDone)
    {
        if (channel > channelMax)
            return RequestStatus::invalidParameter;

        if (onStartReceiverTestDone)
            return RequestStatus::busy;

        auto status = hci_le_receiver_test_v2(channel, infra::enum_cast(phy), standardModulationIndex);

        onStartReceiverTestDone = onDone;
        Complete(onStartReceiverTestDone, ResultOf(status));

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::StartTransmitterTest(uint8_t channel, uint8_t dataLength, PacketPayload payload, Phy phy, const infra::Function<void(Result)>& onDone)
    {
        if (channel > channelMax)
            return RequestStatus::invalidParameter;

        if (onStartTransmitterTestDone)
            return RequestStatus::busy;

        auto status = hci_le_transmitter_test_v2(channel, dataLength, infra::enum_cast(payload), infra::enum_cast(phy));

        onStartTransmitterTestDone = onDone;
        Complete(onStartTransmitterTestDone, ResultOf(status));

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::EndTest(const infra::Function<void(Result, uint16_t packetsReceived)>& onDone)
    {
        if (onEndTestDone)
            return RequestStatus::busy;

        uint16_t numberOfPackets = 0;
        auto status = hci_le_test_end(&numberOfPackets);

        onEndTestDone = onDone;
        Complete(onEndTestDone, ResultOf(status), status == BLE_STATUS_SUCCESS ? numberOfPackets : uint16_t{ 0 });

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::StartUnmodulatedCarrier(uint8_t channel, uint8_t offset, const infra::Function<void(Result)>& onDone)
    {
        if (channel > channelMax)
            return RequestStatus::invalidParameter;

        if (onStartUnmodulatedCarrierDone)
            return RequestStatus::busy;

        auto status = aci_hal_tone_start(channel, offset);

        onStartUnmodulatedCarrierDone = onDone;
        Complete(onStartUnmodulatedCarrierDone, ResultOf(status));

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::StopUnmodulatedCarrier(const infra::Function<void(Result)>& onDone)
    {
        if (onStopUnmodulatedCarrierDone)
            return RequestStatus::busy;

        auto status = aci_hal_tone_stop();

        onStopUnmodulatedCarrierDone = onDone;
        Complete(onStopUnmodulatedCarrierDone, ResultOf(status));

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::SetTransmitPowerLevel(int8_t txPower, const infra::Function<void(Result)>& onDone)
    {
#if defined(STM32WB)
        if (onSetTransmitPowerLevelDone)
            return RequestStatus::busy;

        constexpr uint8_t standardPower = 0x00u;
        auto status = aci_hal_set_tx_power_level(standardPower, NearestPaLevel(txPower));

        onSetTransmitPowerLevelDone = onDone;
        Complete(onSetTransmitPowerLevelDone, ResultOf(status));

        return RequestStatus::accepted;
#else
        // ACI_HAL_SET_TX_POWER_LEVEL takes a PA level rather than a power in dBm, and on WBA the
        // table relating the two is not in the headers of this middleware: it depends on the
        // selected power mode and is published separately. Guessing it would report a transmit
        // power the radio is not producing, which is the one thing a conformance measurement must
        // be able to trust.
        return RequestStatus::notSupported;
#endif
    }
}
