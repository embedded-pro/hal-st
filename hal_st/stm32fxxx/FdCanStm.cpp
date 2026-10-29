#include "hal_st/stm32fxxx/FdCanStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <cstdint>

#if defined(HAS_PERIPHERAL_FDCAN)

namespace
{
    constexpr uint32_t minimumTimeQuanta = 8;
    constexpr uint32_t maximumTimeQuanta = 25;
    constexpr uint32_t maximumPrescaler = 512;
    constexpr uint32_t maximumTimeSegment1 = 256;
    constexpr uint32_t maximumTimeSegment2 = 128;
    constexpr uint8_t maxDataLength = 8;

    // A classic frame may carry a DLC of 9 to 15, for which the HAL copies the FD length of the element
    constexpr std::size_t maxElementData = 64;

    // The HAL encodes the data length differently across families, so lengths go through its own constants
    constexpr std::array<uint32_t, maxDataLength + 1> dataLengthCodes{ {
        FDCAN_DLC_BYTES_0,
        FDCAN_DLC_BYTES_1,
        FDCAN_DLC_BYTES_2,
        FDCAN_DLC_BYTES_3,
        FDCAN_DLC_BYTES_4,
        FDCAN_DLC_BYTES_5,
        FDCAN_DLC_BYTES_6,
        FDCAN_DLC_BYTES_7,
        FDCAN_DLC_BYTES_8,
    } };

    constexpr uint32_t interrupts = FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_TX_COMPLETE | FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_BUS_OFF | FDCAN_IT_ARB_PROTOCOL_ERROR;
    constexpr uint32_t allTxBuffers = FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2;

    uint8_t DataLength(uint32_t dataLengthCode)
    {
        const auto code = std::find(dataLengthCodes.begin(), dataLengthCodes.end(), dataLengthCode);
        return code == dataLengthCodes.end() ? maxDataLength : static_cast<uint8_t>(std::distance(dataLengthCodes.begin(), code));
    }

    std::optional<hal::FdCanStm::Error> LastErrorCodeToError(uint32_t lastErrorCode)
    {
        switch (lastErrorCode)
        {
            case FDCAN_PROTOCOL_ERROR_STUFF:
                return hal::FdCanStm::Error::stuffError;
            case FDCAN_PROTOCOL_ERROR_FORM:
                return hal::FdCanStm::Error::formError;
            case FDCAN_PROTOCOL_ERROR_ACK:
                return hal::FdCanStm::Error::ackError;
            case FDCAN_PROTOCOL_ERROR_BIT1:
                return hal::FdCanStm::Error::bit1Error;
            case FDCAN_PROTOCOL_ERROR_BIT0:
                return hal::FdCanStm::Error::bit0Error;
            case FDCAN_PROTOCOL_ERROR_CRC:
                return hal::FdCanStm::Error::crcError;
            default:
                return std::nullopt;
        }
    }
}

namespace hal
{
    // Sample point near 87.5 %, the CiA recommendation; the most time quanta that divide the clock exactly win
    FdCanStm::NominalBitTiming FdCanStm::CalculateNominalBitTiming(uint32_t kernelClock, BitRate bitRate)
    {
        really_assert(bitRate != 0);

        uint32_t bestQuanta = maximumTimeQuanta;
        uint32_t bestPrescaler = 1;
        uint32_t bestError = UINT32_MAX;

        for (uint32_t quanta = maximumTimeQuanta; quanta >= minimumTimeQuanta; --quanta)
        {
            const auto quantumRate = static_cast<uint64_t>(bitRate) * quanta;
            const auto prescaler = std::clamp<uint64_t>((kernelClock + quantumRate / 2) / quantumRate, 1, maximumPrescaler);
            const auto actual = kernelClock / (prescaler * quanta);
            const auto error = static_cast<uint32_t>(actual > bitRate ? actual - bitRate : bitRate - actual);

            if (error < bestError)
            {
                bestError = error;
                bestQuanta = quanta;
                bestPrescaler = static_cast<uint32_t>(prescaler);
            }

            if (error == 0)
                break;
        }

        const auto timeSegment2 = std::max<uint32_t>(1, bestQuanta / 8);
        return { bestPrescaler, bestQuanta - 1 - timeSegment2, timeSegment2, timeSegment2 };
    }

    FdCanStm::FdCanStm(infra::MemoryRange<FdCanRxEntry> rxStorage, uint8_t oneBasedIndex, GpioPinStm& rxPin, GpioPinStm& txPin, const Config& config, const infra::Function<void(Error)>& onError)
        : rxQueue(rxStorage, [this]()
              {
                  ProcessRxBuffer();
              })
        , index(static_cast<uint8_t>(oneBasedIndex - 1))
        , rx(rxPin, PinConfigTypeStm::canRx, oneBasedIndex)
        , tx(txPin, PinConfigTypeStm::canTx, oneBasedIndex)
        , config(config)
        , onError(onError)
    {
        really_assert(oneBasedIndex >= 1 && oneBasedIndex <= peripheralfdCan.size());

        EnableClockfdCan(index);
        handle.Instance = peripheralfdCan[index];

        Initialize();

        // Served in the interrupt, not dispatched: a receive FIFO holds three frames, fewer than arrive at 1 Mbit/s
        // while the event dispatcher runs a long action
        Register(peripheralfdCanIt0Irq[index], this->config.interruptPriority);
    }

    FdCanStm::~FdCanStm()
    {
        Unregister();

        HAL_FDCAN_Stop(&handle);
        HAL_FDCAN_DeInit(&handle);
        DisableClockfdCan(index);
    }

    void FdCanStm::Initialize()
    {
        const auto timing = std::holds_alternative<NominalBitTiming>(config.timing)
                                ? std::get<NominalBitTiming>(config.timing)
                                : CalculateNominalBitTiming(HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN), std::get<BitRate>(config.timing));

        really_assert(timing.prescaler >= 1 && timing.prescaler <= maximumPrescaler);
        really_assert(timing.timeSegment1 >= 1 && timing.timeSegment1 <= maximumTimeSegment1);
        really_assert(timing.timeSegment2 >= 1 && timing.timeSegment2 <= maximumTimeSegment2);
        really_assert(timing.syncJumpWidth >= 1 && timing.syncJumpWidth <= timing.timeSegment2);

        handle.Init.ClockDivider = FDCAN_CLOCK_DIV1;
        handle.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
        handle.Init.Mode = config.testMode ? FDCAN_MODE_EXTERNAL_LOOPBACK : FDCAN_MODE_NORMAL;
        handle.Init.AutoRetransmission = ENABLE;
        handle.Init.TransmitPause = DISABLE;
        handle.Init.ProtocolException = DISABLE;
        handle.Init.NominalPrescaler = timing.prescaler;
        handle.Init.NominalSyncJumpWidth = timing.syncJumpWidth;
        handle.Init.NominalTimeSeg1 = timing.timeSegment1;
        handle.Init.NominalTimeSeg2 = timing.timeSegment2;
        handle.Init.DataPrescaler = 1;
        handle.Init.DataSyncJumpWidth = 1;
        handle.Init.DataTimeSeg1 = 1;
        handle.Init.DataTimeSeg2 = 1;
        handle.Init.StdFiltersNbr = config.filter && !config.filter->extended ? 1 : 0;
        handle.Init.ExtFiltersNbr = config.filter && config.filter->extended ? 1 : 0;
        handle.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

        auto result = HAL_FDCAN_Init(&handle);
        really_assert(result == HAL_OK);

        ConfigureFilter();

        result = HAL_FDCAN_ActivateNotification(&handle, interrupts, allTxBuffers);
        really_assert(result == HAL_OK);

        result = HAL_FDCAN_Start(&handle);
        really_assert(result == HAL_OK);
    }

    void FdCanStm::ConfigureFilter()
    {
        if (!config.filter)
        {
            auto result = HAL_FDCAN_ConfigGlobalFilter(&handle, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
            really_assert(result == HAL_OK);
            return;
        }

        FDCAN_FilterTypeDef filter{};
        filter.IdType = config.filter->extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        filter.FilterIndex = 0;
        filter.FilterType = FDCAN_FILTER_MASK;
        filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
        filter.FilterID1 = config.filter->id;
        filter.FilterID2 = config.filter->mask;

        auto result = HAL_FDCAN_ConfigFilter(&handle, &filter);
        really_assert(result == HAL_OK);

        const auto otherIdType = config.filter->matchIdType ? FDCAN_REJECT : FDCAN_ACCEPT_IN_RX_FIFO0;
        const auto nonMatchingStandard = config.filter->extended ? otherIdType : FDCAN_REJECT;
        const auto nonMatchingExtended = config.filter->extended ? FDCAN_REJECT : otherIdType;

        result = HAL_FDCAN_ConfigGlobalFilter(&handle, nonMatchingStandard, nonMatchingExtended, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
        really_assert(result == HAL_OK);
    }

    void FdCanStm::SendData(Id id, const Message& data, const infra::Function<void(bool success)>& actionOnCompletion)
    {
        really_assert(!sending.load(std::memory_order_acquire));
        really_assert(data.size() <= maxDataLength);

        onSendComplete = actionOnCompletion;
        sending.store(true, std::memory_order_release);

        FDCAN_TxHeaderTypeDef header{};
        header.Identifier = id.Is29BitId() ? id.Get29BitId() : id.Get11BitId();
        header.IdType = id.Is29BitId() ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        header.DataLength = dataLengthCodes[data.size()];
        header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        header.BitRateSwitch = FDCAN_BRS_OFF;
        header.FDFormat = FDCAN_CLASSIC_CAN;
        header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        header.MessageMarker = 0;

        std::array<uint8_t, maxDataLength> payload{};
        std::copy(data.begin(), data.end(), payload.begin());

        if (HAL_FDCAN_AddMessageToTxFifoQ(&handle, &header, payload.data()) != HAL_OK)
            NotifySendFailedFromInterrupt();
    }

    void FdCanStm::ReceiveData(const infra::Function<void(Id id, const Message& data)>& receivedAction)
    {
        onReceive = receivedAction;
    }

    bool FdCanStm::InterruptPending(uint32_t flag, uint32_t interrupt)
    {
        if (__HAL_FDCAN_GET_FLAG(&handle, flag) == 0 || __HAL_FDCAN_GET_IT_SOURCE(&handle, interrupt) == 0)
            return false;

        __HAL_FDCAN_CLEAR_FLAG(&handle, flag);
        return true;
    }

    void FdCanStm::Invoke()
    {
        if (InterruptPending(FDCAN_FLAG_RX_FIFO0_NEW_MESSAGE, FDCAN_IT_RX_FIFO0_NEW_MESSAGE))
            HandleRxInterrupt();

        if (InterruptPending(FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST, FDCAN_IT_RX_FIFO0_MESSAGE_LOST))
            ScheduleError(Error::messageLost);

        if (InterruptPending(FDCAN_FLAG_TX_COMPLETE, FDCAN_IT_TX_COMPLETE))
            HandleTxInterrupt();

        const auto warning = InterruptPending(FDCAN_FLAG_ERROR_WARNING, FDCAN_IT_ERROR_WARNING);
        const auto passive = InterruptPending(FDCAN_FLAG_ERROR_PASSIVE, FDCAN_IT_ERROR_PASSIVE);
        const auto busOffChanged = InterruptPending(FDCAN_FLAG_BUS_OFF, FDCAN_IT_BUS_OFF);
        const auto protocol = InterruptPending(FDCAN_FLAG_ARB_PROTOCOL_ERROR, FDCAN_IT_ARB_PROTOCOL_ERROR);

        if (warning || passive || busOffChanged || protocol)
            HandleStatusInterrupt();
    }

    void FdCanStm::HandleStatusInterrupt()
    {
        FDCAN_ProtocolStatusTypeDef status{};
        HAL_FDCAN_GetProtocolStatus(&handle, &status);

        if (auto error = LastErrorCodeToError(status.LastErrorCode))
            ScheduleError(*error);

        const bool warning = status.Warning != 0;
        if (warning && !previousWarning)
            ScheduleError(Error::errorWarning);
        previousWarning = warning;

        const bool passive = status.ErrorPassive != 0;
        if (passive && !previousPassive)
            ScheduleError(Error::errorPassive);
        previousPassive = passive;

        if (status.BusOff != 0 && !busOff)
            HandleBusOff();
        else if (status.BusOff == 0 && busOff)
        {
            busOff = false;
            HAL_FDCAN_ActivateNotification(&handle, FDCAN_IT_ARB_PROTOCOL_ERROR, 0);
        }
    }

    void FdCanStm::HandleBusOff()
    {
        busOff = true;
        ScheduleError(Error::busOff);

        // The recovery sequence reports a protocol error every 11 recessive bits until it ends
        HAL_FDCAN_DeactivateNotification(&handle, FDCAN_IT_ARB_PROTOCOL_ERROR);

        // Cancelled so that recovery does not send the frame after its failure was reported
        HAL_FDCAN_AbortTxRequest(&handle, allTxBuffers);
        NotifySendFailedFromInterrupt();

        // Bus-off leaves the controller in initialisation; leaving it starts the recovery sequence
        if (config.autoBusOffRecovery)
        {
            HAL_FDCAN_Stop(&handle);
            HAL_FDCAN_Start(&handle);
        }
    }

    void FdCanStm::HandleTxInterrupt()
    {
        if (sending.exchange(false, std::memory_order_acq_rel))
            QueueSendCompletion(true);
    }

    void FdCanStm::HandleRxInterrupt()
    {
        FDCAN_RxHeaderTypeDef header{};
        std::array<uint8_t, maxElementData> payload{};

        while (HAL_FDCAN_GetRxFifoFillLevel(&handle, FDCAN_RX_FIFO0) != 0)
        {
            if (HAL_FDCAN_GetRxMessage(&handle, FDCAN_RX_FIFO0, &header, payload.data()) != HAL_OK)
                return;

            if (rxQueue.Full())
            {
                ScheduleError(Error::messageLost);
                continue;
            }

            FdCanRxEntry entry{};
            entry.is29Bit = header.IdType == FDCAN_EXTENDED_ID;
            entry.id = header.Identifier;
            entry.length = DataLength(header.DataLength);
            std::copy_n(payload.begin(), entry.length, entry.data.begin());
            rxQueue.AddFromInterrupt(entry);
        }
    }

    void FdCanStm::ProcessRxBuffer()
    {
        while (!rxQueue.Empty())
        {
            const FdCanRxEntry entry = rxQueue.Get();
            const Id id = entry.is29Bit ? Id::Create29BitId(entry.id) : Id::Create11BitId(entry.id);

            Message data;
            for (uint8_t i = 0; i != entry.length; ++i)
                data.push_back(entry.data[i]);

            if (onReceive)
                onReceive(id, data);
        }
    }

    // A bus nobody acknowledges reports an error per retransmission; one pending notification per class keeps the event queue bounded
    void FdCanStm::ScheduleError(Error error) const
    {
        const auto bit = static_cast<uint16_t>(1u << static_cast<uint8_t>(error));

        if ((pendingErrors.fetch_or(bit, std::memory_order_acq_rel) & bit) != 0)
            return;

        infra::EventDispatcher::Instance().Schedule([this, error, bit]()
            {
                pendingErrors.fetch_and(static_cast<uint16_t>(~bit), std::memory_order_acq_rel);

                if (onError)
                    onError(error);
            });
    }

    void FdCanStm::NotifySendFailedFromInterrupt()
    {
        if (sending.exchange(false, std::memory_order_acq_rel))
            QueueSendCompletion(false);
    }

    // The callback is copied here because the next SendData may rebind onSendComplete before the dispatcher runs the completion
    void FdCanStm::QueueSendCompletion(bool success)
    {
        const auto end = sendCompletionsEnd.load(std::memory_order_relaxed);
        really_assert(static_cast<uint8_t>(end - sendCompletionsBegin.load(std::memory_order_acquire)) < sendCompletions.size());

        sendCompletions[end % sendCompletions.size()] = SendCompletion{ onSendComplete, success };
        sendCompletionsEnd.store(static_cast<uint8_t>(end + 1), std::memory_order_release);

        infra::EventDispatcher::Instance().Schedule([this]()
            {
                DispatchSendCompletion();
            });
    }

    void FdCanStm::DispatchSendCompletion()
    {
        const auto begin = sendCompletionsBegin.load(std::memory_order_relaxed);
        const SendCompletion completion = sendCompletions[begin % sendCompletions.size()];
        sendCompletionsBegin.store(static_cast<uint8_t>(begin + 1), std::memory_order_release);

        if (completion.action)
            completion.action(completion.success);
    }
}

#endif
