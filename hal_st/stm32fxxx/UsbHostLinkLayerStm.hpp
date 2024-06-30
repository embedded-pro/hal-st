#ifndef HAL_USB_HOST_LINK_LAYER_STM_HPP
#define HAL_USB_HOST_LINK_LAYER_STM_HPP

#include DEVICE_HEADER
#include "infra/util/AutoResetFunction.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/UsbLinkLayer.hpp"
#include "hal_st/cortex/InterruptCortex.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/InterfaceConnector.hpp"
#include "infra/util/Variant.hpp"

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
            bool useDma{ false };
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

        struct Ulpi
        {
            hal::GpioPinStm& clock;
            hal::GpioPinStm& direction;
            hal::GpioPinStm& dataStreamStop;
            hal::GpioPinStm& dataStreamNextRequest;
            hal::GpioPinStm& d0;
            hal::GpioPinStm& d1;
            hal::GpioPinStm& d2;
            hal::GpioPinStm& d3;
            hal::GpioPinStm& d4;
            hal::GpioPinStm& d5;
            hal::GpioPinStm& d6;
            hal::GpioPinStm& d7;
        };

        UsbHostLinkLayerStm(Type usbType, hal::GpioPinStm& id, hal::GpioPinStm& dm, hal::GpioPinStm& dp, const Config& config = Config());
        UsbHostLinkLayerStm(Type usbType, Ulpi ulpiPins, const Config& config = Config());
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
        void Initialize(uint32_t phyInterface, const Config& config);
        void CreateInterruptDispatched();
        void DestroyInterruptDispatched();

    private:
        struct InternalPhy
        {
            InternalPhy(Type usbType, hal::GpioPinStm& id, hal::GpioPinStm& dm, hal::GpioPinStm& dp)
                : id(id, usbType == Type::fullSpeed ? hal::PinConfigTypeStm::usbFsId : hal::PinConfigTypeStm::usbHsId, 0)
                , dm(dm, usbType == Type::fullSpeed ? hal::PinConfigTypeStm::usbFsDm : hal::PinConfigTypeStm::usbHsDm, 0)
                , dp(dp, usbType == Type::fullSpeed ? hal::PinConfigTypeStm::usbFsDp : hal::PinConfigTypeStm::usbHsDp, 0)
            {}

            hal::PeripheralPinStm id;
            hal::PeripheralPinStm dm;
            hal::PeripheralPinStm dp;
        };

        struct ExternalPhy
        {
            explicit ExternalPhy(Ulpi pins)
                : clock(pins.clock, hal::PinConfigTypeStm::usbHsUlpiClk, 0)
                , direction(pins.direction, hal::PinConfigTypeStm::usbHsUlpiDir, 0)
                , dataStreamStop(pins.dataStreamStop, hal::PinConfigTypeStm::usbHsUlpiStp, 0)
                , dataStreamNextRequest(pins.dataStreamNextRequest, hal::PinConfigTypeStm::usbHsUlpiNxt, 0)
                , d0(pins.d0, hal::PinConfigTypeStm::usbHsUlpiD0, 0)
                , d1(pins.d1, hal::PinConfigTypeStm::usbHsUlpiD1, 0)
                , d2(pins.d2, hal::PinConfigTypeStm::usbHsUlpiD2, 0)
                , d3(pins.d3, hal::PinConfigTypeStm::usbHsUlpiD3, 0)
                , d4(pins.d4, hal::PinConfigTypeStm::usbHsUlpiD4, 0)
                , d5(pins.d5, hal::PinConfigTypeStm::usbHsUlpiD5, 0)
                , d6(pins.d6, hal::PinConfigTypeStm::usbHsUlpiD6, 0)
                , d7(pins.d7, hal::PinConfigTypeStm::usbHsUlpiD7, 0)
            {}

            hal::PeripheralPinStm clock;
            hal::PeripheralPinStm direction;
            hal::PeripheralPinStm dataStreamStop;
            hal::PeripheralPinStm dataStreamNextRequest;
            hal::PeripheralPinStm d0;
            hal::PeripheralPinStm d1;
            hal::PeripheralPinStm d2;
            hal::PeripheralPinStm d3;
            hal::PeripheralPinStm d4;
            hal::PeripheralPinStm d5;
            hal::PeripheralPinStm d6;
            hal::PeripheralPinStm d7;
        };

        struct channelCallbacks
        {
            infra::AutoResetFunction<void(UsbRequestBlockState)> onTransmissionDone;
            infra::AutoResetFunction<void(infra::ConstByteRange, UsbRequestBlockState)> onReceptionDone;
        };

        static constexpr std::size_t maxReceptionBufferSize = 1024;
        static constexpr std::size_t numberOfChannels = 11;

        uint8_t usbIndex;
        infra::Variant<InternalPhy, ExternalPhy> pins;
        HCD_HandleTypeDef hcd;
        std::array<channelCallbacks, numberOfChannels> channelCallbacks;
        infra::Optional<DispatchedInterruptHandler> dispatchedInterruptHandler;
        std::array<uint8_t, maxReceptionBufferSize> receptionBuffer;
    };
}

#endif

#endif
