#include "hal_st/stm32fxxx/UsbHostLinkLayerStm.hpp"
#include "hal/interfaces/UsbLinkLayer.hpp"
#include "infra/util/ByteRange.hpp"

#ifdef HAS_PERIPHERAL_USB

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef* hcd)
{
    static_cast<hal::UsbHostLinkLayerStm*>(hcd->pData)->StartOfFrame();
}

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef* hcd)
{
    static_cast<hal::UsbHostLinkLayerStm*>(hcd->pData)->Connected();
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef* hcd)
{
    static_cast<hal::UsbHostLinkLayerStm*>(hcd->pData)->Disconnected();
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef* hcd)
{
    static_cast<hal::UsbHostLinkLayerStm*>(hcd->pData)->PortEnabled();
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef* hcd)
{
    static_cast<hal::UsbHostLinkLayerStm*>(hcd->pData)->PortDisabled();
}

void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef* hcd, uint8_t chnum, HCD_URBStateTypeDef urb_state)
{
    static_cast<hal::UsbHostLinkLayerStm*>(hcd->pData)->UrbChanged(chnum, urb_state);
}

namespace
{
    hal::UsbSpeed ToSpeed(uint32_t speed)
    {
        if (speed == HCD_DEVICE_SPEED_FULL)
            return  hal::UsbSpeed::full;

        if (speed == HCD_DEVICE_SPEED_HIGH)
            return hal::UsbSpeed::high;

        return hal::UsbSpeed::low;
    }

    uint8_t ToSpeed(hal::UsbSpeed speed)
    {
        if (speed == hal::UsbSpeed::full)
            return HCD_DEVICE_SPEED_FULL;

        if (speed == hal::UsbSpeed::high)
            return HCD_DEVICE_SPEED_HIGH;

        return HCD_DEVICE_SPEED_LOW;
    }

    uint8_t ToEndPointType(hal::UsbEndPointType type)
    {
        if (type == hal::UsbEndPointType::bulk)
            return EP_TYPE_BULK;

        if (type == hal::UsbEndPointType::control)
            return EP_TYPE_CTRL;

        if (type == hal::UsbEndPointType::interrupt)
            return EP_TYPE_INTR;

        return EP_TYPE_ISOC;
    }

    uint8_t ToToken(hal::UsbHostLinkLayer::Pid token)
    {
        if (token == hal::UsbHostLinkLayer::Pid::data)
            return 1;
        else
            return 0;
    }

    hal::UsbHostLinkLayer::UsbRequestBlockState ToUrbState(uint8_t state)
    {
        if (state == URB_DONE)
            return hal::UsbHostLinkLayer::UsbRequestBlockState::success;

        if (state == URB_STALL)
            return hal::UsbHostLinkLayer::UsbRequestBlockState::stall;

        if (state == URB_NOTREADY)
            return hal::UsbHostLinkLayer::UsbRequestBlockState::notReady;

        return hal::UsbHostLinkLayer::UsbRequestBlockState::error;
    }

    constexpr std::array<unsigned int, 2> peripheralUSBArray =
    {{
        USB_OTG_FS_PERIPH_BASE,
        USB_OTG_HS_PERIPH_BASE
    }};

    const infra::MemoryRange<USB_OTG_GlobalTypeDef* const> peripheralUSBLocal = infra::ReinterpretCastMemoryRange<USB_OTG_GlobalTypeDef* const>(infra::MakeRange(peripheralUSBArray));

    constexpr std::array<IRQn_Type const, 2> peripheralUSBIrqArray =
    {{
        OTG_FS_IRQn,
        OTG_HS_IRQn,
    }};

    constexpr infra::MemoryRange<IRQn_Type const> peripheralUSBIrq = peripheralUSBIrqArray;

    void EnableClockUSBLocally(std::size_t index)
    {
        switch (index)
        {
            case 0: __HAL_RCC_USB_OTG_FS_CLK_ENABLE(); break;
            case 1: __HAL_RCC_USB_OTG_HS_CLK_ENABLE(); break;
            default: std::abort();
        }
    }

    void DisableClockUSBLocally(std::size_t index)
    {
        switch (index)
        {
            case 0: __HAL_RCC_USB_OTG_FS_CLK_DISABLE(); break;
            case 1: __HAL_RCC_USB_OTG_HS_CLK_DISABLE(); break;
            default: std::abort();
        }
    }
}

namespace hal
{
    UsbHostLinkLayerStm::UsbHostLinkLayerStm(Type usbType, hal::GpioPinStm& id, hal::GpioPinStm& dm, hal::GpioPinStm& dp, const Config& config)
        : usbIndex(static_cast<uint8_t>(usbType))
        , pins{ infra::InPlaceType<InternalPhy>(), usbType, id, dm, dp }
    {
        Initialize(HCD_PHY_EMBEDDED, config);
    }

    UsbHostLinkLayerStm::UsbHostLinkLayerStm(Type usbType, Ulpi pins, const Config& config)
        : usbIndex(static_cast<uint8_t>(usbType))
        , pins{ infra::InPlaceType<ExternalPhy>(), pins }
    {
        Initialize(HCD_PHY_ULPI, config);
    }

    UsbHostLinkLayerStm::~UsbHostLinkLayerStm()
    {
        HAL_HCD_Stop(&hcd);
        HAL_HCD_DeInit(&hcd);
        DestroyInterruptDispatched();
        DisableClockUSBLocally(usbIndex);
    }

    void UsbHostLinkLayerStm::Initialize(uint32_t phyInterface, const Config& config)
    {
        hcd.Instance = peripheralUSBLocal[usbIndex];
        hcd.Init.Host_channels = numberOfChannels;
        hcd.pData = this;
        hcd.Init.dma_enable = config.useDma;
        hcd.Init.low_power_enable = config.lowPower;
        hcd.Init.phy_itface = phyInterface;
        hcd.Init.Sof_enable = config.startOfFrame;
        hcd.Init.speed = config.speed;
        hcd.Init.use_external_vbus = config.externalVBus;

        EnableClockUSBLocally(usbIndex);
        HAL_HCD_Init(&hcd);
        CreateInterruptDispatched();
        HAL_HCD_Start(&hcd);
    }

    void UsbHostLinkLayerStm::CreateInterruptDispatched()
    {
        dispatchedInterruptHandler.Emplace(peripheralUSBIrq[usbIndex], [this]()
            {
                HAL_HCD_IRQHandler(&hcd);
            });
    }

    void UsbHostLinkLayerStm::DestroyInterruptDispatched()
    {
        dispatchedInterruptHandler = infra::none;
    }

    UsbSpeed UsbHostLinkLayerStm::Speed()
    {
        return ToSpeed(HAL_HCD_GetCurrentSpeed(&hcd));
    }

    void UsbHostLinkLayerStm::ResetPort()
    {
        HAL_HCD_ResetPort(&hcd);
    }

    void UsbHostLinkLayerStm::Open(uint8_t pipe, uint8_t endPoint, uint8_t address, UsbSpeed speed, UsbEndPointType type, uint16_t maxPacketSize)
    {
        HAL_HCD_HC_Init(&hcd, pipe, endPoint, address, ToSpeed(speed), ToEndPointType(type), maxPacketSize);
    }

    void UsbHostLinkLayerStm::Close(uint8_t pipe)
    {
        HAL_HCD_HC_Halt(&hcd, pipe);
    }

    void UsbHostLinkLayerStm::Transmit(uint8_t pipe, UsbEndPointType type, Pid token, infra::ConstByteRange buffer, bool ping, const infra::Function<void(UsbRequestBlockState)>& onDone)
    {
        channelCallbacks.at(pipe).onTransmissionDone = onDone;
        HAL_HCD_HC_SubmitRequest(&hcd,pipe, 0, ToEndPointType(type), ToToken(token), const_cast<uint8_t *>(buffer.begin()), static_cast<uint16_t>(buffer.size()), ping);
    }

    void UsbHostLinkLayerStm::Receive(uint8_t pipe, UsbEndPointType type, Pid token, bool ping, const infra::Function<void(infra::ConstByteRange, UsbRequestBlockState)>& onDone)
    {
        channelCallbacks.at(pipe).onReceptionDone = onDone;
        HAL_HCD_HC_SubmitRequest(&hcd,pipe, 1, ToEndPointType(type), ToToken(token), receptionBuffer.data(), static_cast<uint16_t>(receptionBuffer.size()), 0);
    }

    void UsbHostLinkLayerStm::SetToggle(uint8_t pipe, bool toggle)
    {
        if (hcd.hc[pipe].ep_is_in)
            hcd.hc[pipe].toggle_in = toggle;
        else
            hcd.hc[pipe].toggle_out = toggle;
    }

    bool UsbHostLinkLayerStm::Toggle(uint8_t pipe)
    {
        if (hcd.hc[pipe].ep_is_in)
            return hcd.hc[pipe].toggle_in;
        else
            return hcd.hc[pipe].toggle_out;
    }

    void UsbHostLinkLayerStm::StartOfFrame() const
    {
        if (HasObserver())
            GetObserver().StartOfFrame();
    }

    void UsbHostLinkLayerStm::Connected() const
    {
        if (HasObserver())
            GetObserver().Connected();
    }

    void UsbHostLinkLayerStm::Disconnected() const
    {
        if (HasObserver())
            GetObserver().Disconnected();
    }

    void UsbHostLinkLayerStm::PortEnabled() const
    {
        if (HasObserver())
            GetObserver().PortEnabled();
    }

    void UsbHostLinkLayerStm::PortDisabled() const
    {
        if (HasObserver())
            GetObserver().PortDisabled();
    }

    void UsbHostLinkLayerStm::UrbChanged(uint8_t pipe, uint8_t urbState)
    {
        auto& callbacks = channelCallbacks.at(pipe);

        if (callbacks.onReceptionDone)
            callbacks.onReceptionDone(infra::ConstByteRange(receptionBuffer.data(), receptionBuffer.data() + HAL_HCD_HC_GetXferCount(&hcd, pipe)), ToUrbState(urbState));
        else if (callbacks.onTransmissionDone)
            callbacks.onTransmissionDone(ToUrbState(urbState));
    }
}

#endif
