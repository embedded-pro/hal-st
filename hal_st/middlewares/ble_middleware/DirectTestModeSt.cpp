#include "hal_st/middlewares/ble_middleware/DirectTestModeSt.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/EnumCast.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

#if defined(STM32WBA)
#include "hal_st/middlewares/ble_middleware/LinkLayerPlatformWba.hpp"
#endif

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

    // The command carries out the procedure, so a controller that refuses it has refused the
    // request: the status answers the call and onDone is never reached.
    services::DirectTestMode::RequestStatus RequestStatusOf(tBleStatus status)
    {
        if (status == BLE_STATUS_SUCCESS)
            return services::DirectTestMode::RequestStatus::accepted;
        if (status == BLE_STATUS_INVALID_PARAMS)
            return services::DirectTestMode::RequestStatus::invalidParameter;
        if (status == BLE_STATUS_BUSY || status == BLE_STATUS_INSUFFICIENT_RESOURCES)
            return services::DirectTestMode::RequestStatus::busy;

        return services::DirectTestMode::RequestStatus::invalidState;
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

    // PA_Level to output power, in tenths of a dBm, as ACI_HAL_SET_TX_POWER_LEVEL documents it.
    // The values are indicative and depend on the device and its package, so a request is answered
    // with the level whose documented power is nearest.
#if defined(STM32WB)
    constexpr std::array<int16_t, 32> paLevelPower{ -400, -209, -198, -189, -176, -165, -153, -141,
        -132, -121, -109, -99, -89, -78, -69, -59,
        -50, -40, -32, -25, -18, -13, -9, -5,
        -2, 0, 10, 20, 30, 40, 50, 60 };

    int16_t PowerCeiling()
    {
        return std::numeric_limits<int16_t>::max();
    }
#else
    // STM32WBA_BLE_Wireless_Interface.html, annex "TX Power Level"
    constexpr std::array<int16_t, 36> paLevelPower{ -204, -204, -204, -204, -204, -204, -194, -183,
        -173, -164, -154, -143, -133, -123, -114, -104,
        -95, -84, -75, -65, -54, -45, -35, -24,
        -15, -3, 9, 23, 28, 39, 48, 56,
        69, 75, 85, 100 };

    // The selected TX power table caps what the radio produces, so levels above it are not offered
    int16_t PowerCeiling()
    {
        return static_cast<int16_t>(hal::LinkLayerPlatformWba::Instance().MaxTransmitPower() * 10);
    }
#endif

    uint8_t NearestPaLevel(int8_t txPower, int16_t ceiling)
    {
        auto wanted = static_cast<int16_t>(txPower * 10);
        auto available = std::find_if(paLevelPower.begin(), paLevelPower.end(), [ceiling](auto power)
            {
                return power > ceiling;
            });
        auto nearest = std::min_element(paLevelPower.begin(), available, [wanted](auto left, auto right)
            {
                return std::abs(left - wanted) < std::abs(right - wanted);
            });

        return static_cast<uint8_t>(std::distance(paLevelPower.begin(), nearest));
    }
}

namespace hal
{
    services::DirectTestMode::RequestStatus DirectTestModeSt::StartReceiverTest(uint8_t channel, Phy phy, const infra::Function<void(Result)>& onDone)
    {
        if (channel > channelMax)
            return RequestStatus::invalidParameter;

        if (onStartReceiverTestDone)
            return RequestStatus::busy;

        auto status = RequestStatusOf(hci_le_receiver_test_v2(channel, infra::enum_cast(phy), standardModulationIndex));

        if (status != RequestStatus::accepted)
            return status;

        onStartReceiverTestDone = onDone;
        Complete(onStartReceiverTestDone, Result::success);

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::StartTransmitterTest(uint8_t channel, uint8_t dataLength, PacketPayload payload, Phy phy, const infra::Function<void(Result)>& onDone)
    {
        if (channel > channelMax)
            return RequestStatus::invalidParameter;

        if (onStartTransmitterTestDone)
            return RequestStatus::busy;

        auto status = RequestStatusOf(hci_le_transmitter_test_v2(channel, dataLength, infra::enum_cast(payload), infra::enum_cast(phy)));

        if (status != RequestStatus::accepted)
            return status;

        onStartTransmitterTestDone = onDone;
        Complete(onStartTransmitterTestDone, Result::success);

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::EndTest(const infra::Function<void(Result, uint16_t packetsReceived)>& onDone)
    {
        if (onEndTestDone)
            return RequestStatus::busy;

        uint16_t numberOfPackets = 0;
        auto status = RequestStatusOf(hci_le_test_end(&numberOfPackets));

        if (status != RequestStatus::accepted)
            return status;

        onEndTestDone = onDone;
        Complete(onEndTestDone, Result::success, numberOfPackets);

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::StartUnmodulatedCarrier(uint8_t channel, uint8_t offset, const infra::Function<void(Result)>& onDone)
    {
        if (channel > channelMax)
            return RequestStatus::invalidParameter;

        if (onStartUnmodulatedCarrierDone)
            return RequestStatus::busy;

        auto status = RequestStatusOf(aci_hal_tone_start(channel, offset));

        if (status != RequestStatus::accepted)
            return status;

        onStartUnmodulatedCarrierDone = onDone;
        Complete(onStartUnmodulatedCarrierDone, Result::success);

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::StopUnmodulatedCarrier(const infra::Function<void(Result)>& onDone)
    {
        if (onStopUnmodulatedCarrierDone)
            return RequestStatus::busy;

        auto status = RequestStatusOf(aci_hal_tone_stop());

        if (status != RequestStatus::accepted)
            return status;

        onStopUnmodulatedCarrierDone = onDone;
        Complete(onStopUnmodulatedCarrierDone, Result::success);

        return RequestStatus::accepted;
    }

    services::DirectTestMode::RequestStatus DirectTestModeSt::SetTransmitPowerLevel(int8_t txPower, const infra::Function<void(Result)>& onDone)
    {
        if (onSetTransmitPowerLevelDone)
            return RequestStatus::busy;

        constexpr uint8_t standardPower = 0x00u;
        auto status = RequestStatusOf(aci_hal_set_tx_power_level(standardPower, NearestPaLevel(txPower, PowerCeiling())));

        if (status != RequestStatus::accepted)
            return status;

        onSetTransmitPowerLevelDone = onDone;
        Complete(onSetTransmitPowerLevelDone, Result::success);

        return RequestStatus::accepted;
    }
}
