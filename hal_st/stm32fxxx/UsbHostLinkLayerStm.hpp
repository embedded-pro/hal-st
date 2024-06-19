#ifndef HAL_USB_HOST_LINK_LAYER_STM_HPP
#define HAL_USB_HOST_LINK_LAYER_STM_HPP

#include DEVICE_HEADER
#include "infra/util/AutoResetFunction.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/UsbLinkLayer.hpp"
#include "hal_st/cortex/InterruptCortex.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/InterfaceConnector.hpp"

#ifdef HAS_PERIPHERAL_USB

namespace hal
{
    namespace detail
    {
        struct UsbHostStmConfig
        {
            bool lowPower{ false };
            bool startOfFrame{ false };
            bool externalVBus{ true };
            uint32_t phyInterface{ HCD_PHY_EMBEDDED };
            uint32_t speed{ HCD_SPEED_HIGH };
            InterruptPriority priority{ InterruptPriority::Normal };
        };
    }

    class UsbHostLinkLayerStm
        : public UsbHostLinkLayer
        , public infra::InterfaceConnector<UsbHostLinkLayerStm>
    {
    public:
        using Config = detail::UsbHostStmConfig;

        enum class Type : uint8_t
        {
            fullSpeed = 0,
            highSpeed,
        };

        UsbHostLinkLayerStm(Type usbType, hal::GpioPinStm& id, hal::GpioPinStm& dm, hal::GpioPinStm& dp, const Config& config = Config());
        ~UsbHostLinkLayerStm();

        // Implementation of UsbHostLinkLayer
        UsbSpeed Speed() override;
        void ResetPort() override;
        void Open(uint8_t pipe, uint8_t endPoint, uint8_t address, UsbSpeed speed, UsbEndPointType type, uint16_t maxPacketSize) override;
        void Close(uint8_t pipe) override;
        void Transmit(uint8_t pipe, UsbEndPointType type, Pid token, infra::ConstByteRange buffer, bool ping, const infra::Function<void(UsbRequestBlockState)>& onDone) override;
        void Receive(uint8_t pipe, UsbEndPointType type, Pid token, bool ping, const infra::Function<void(infra::ConstByteRange, UsbRequestBlockState)>& onDone) override;
        void SetToggle(uint8_t pipe, bool toggle) override;
        bool Toggle(uint8_t pipe) override;

        void StartOfFrame() const;
        void Connected() const;
        void Disconnected() const;
        void PortEnabled() const;
        void PortDisabled() const;
        void UrbChanged(uint8_t pipe, uint8_t urbState);

    private:
        void CreateInterruptDispatched();
        void DestroyInterruptDispatched();

    private:
        struct channelCallbacks
        {
            infra::AutoResetFunction<void(UsbRequestBlockState)> onTransmissionDone;
            infra::AutoResetFunction<void(infra::ConstByteRange, UsbRequestBlockState)> onReceptionDone;
        };

        static constexpr std::size_t maxReceptionBufferSize = 1024;
        static constexpr std::size_t numberOfChannels = 11;

        uint8_t usbIndex;
        hal::PeripheralPinStm id;
        hal::PeripheralPinStm dm;
        hal::PeripheralPinStm dp;
        HCD_HandleTypeDef hcd;
        std::array<channelCallbacks, numberOfChannels> channelCallbacks;
        infra::Optional<DispatchedInterruptHandler> dispatchedInterruptHandler;
        std::array<uint8_t, maxReceptionBufferSize> receptionBuffer;
    };
}

#endif

#endif
