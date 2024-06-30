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
                : id(id, hal::PinConfigTypeStm::usbFsId, static_cast<uint8_t>(usbType))
                , dm(dm, hal::PinConfigTypeStm::usbFsDm, static_cast<uint8_t>(usbType))
                , dp(dp, hal::PinConfigTypeStm::usbFsDp, static_cast<uint8_t>(usbType))
            {}

            hal::PeripheralPinStm id;
            hal::PeripheralPinStm dm;
            hal::PeripheralPinStm dp;
        };

        struct ExternalPhy
        {
            explicit ExternalPhy(Ulpi pins)
                : clock(pins.clock, hal::PinConfigTypeStm::usbHsUlpiClk, static_cast<uint8_t>(Type::highSpeed))
                , direction(pins.direction, hal::PinConfigTypeStm::usbHsUlpiDir, static_cast<uint8_t>(Type::highSpeed))
                , dataStreamStop(pins.dataStreamStop, hal::PinConfigTypeStm::usbHsUlpiStp, static_cast<uint8_t>(Type::highSpeed))
                , dataStreamNextRequest(pins.dataStreamNextRequest, hal::PinConfigTypeStm::usbHsUlpiNxt, static_cast<uint8_t>(Type::highSpeed))
                , d0(pins.d0, hal::PinConfigTypeStm::usbHsUlpiD0, static_cast<uint8_t>(Type::highSpeed))
                , d1(pins.d1, hal::PinConfigTypeStm::usbHsUlpiD1, static_cast<uint8_t>(Type::highSpeed))
                , d2(pins.d2, hal::PinConfigTypeStm::usbHsUlpiD2, static_cast<uint8_t>(Type::highSpeed))
                , d3(pins.d3, hal::PinConfigTypeStm::usbHsUlpiD3, static_cast<uint8_t>(Type::highSpeed))
                , d4(pins.d4, hal::PinConfigTypeStm::usbHsUlpiD4, static_cast<uint8_t>(Type::highSpeed))
                , d5(pins.d5, hal::PinConfigTypeStm::usbHsUlpiD5, static_cast<uint8_t>(Type::highSpeed))
                , d6(pins.d6, hal::PinConfigTypeStm::usbHsUlpiD6, static_cast<uint8_t>(Type::highSpeed))
                , d7(pins.d7, hal::PinConfigTypeStm::usbHsUlpiD7, static_cast<uint8_t>(Type::highSpeed))
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
