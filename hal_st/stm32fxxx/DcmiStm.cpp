#include "hal_st/stm32fxxx/DcmiStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>
#include <type_traits>

#if defined(HAS_PERIPHERAL_DCMI)

namespace
{
    constexpr uint8_t frameEvent = 1;
    constexpr uint8_t overrunEvent = 2;
    constexpr uint8_t synchronizationEvent = 4;
    constexpr uint8_t errorEvents = overrunEvent | synchronizationEvent;

    constexpr uint32_t maxTransferItems = 0xFFFF;
    constexpr uint32_t wordSize = 4;
#if defined(DMA_STREAM_BASED)
    constexpr uint32_t itemSize = wordSize;
#else
    constexpr uint32_t itemSize = 1;
#endif
    constexpr uintptr_t cacheLineSize = 32;
    constexpr uint32_t flagsToClear = DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI | DCMI_FLAG_LINERI;

#if defined(DMA_STREAM_BASED)
    const std::array dmaChannels{
        std::array{ DMA1_Stream0, DMA1_Stream1, DMA1_Stream2, DMA1_Stream3, DMA1_Stream4, DMA1_Stream5, DMA1_Stream6, DMA1_Stream7 },
        std::array{ DMA2_Stream0, DMA2_Stream1, DMA2_Stream2, DMA2_Stream3, DMA2_Stream4, DMA2_Stream5, DMA2_Stream6, DMA2_Stream7 },
    };

    const std::array dmaIrqs{
        std::array{ DMA1_Stream0_IRQn, DMA1_Stream1_IRQn, DMA1_Stream2_IRQn, DMA1_Stream3_IRQn, DMA1_Stream4_IRQn, DMA1_Stream5_IRQn, DMA1_Stream6_IRQn, DMA1_Stream7_IRQn },
        std::array{ DMA2_Stream0_IRQn, DMA2_Stream1_IRQn, DMA2_Stream2_IRQn, DMA2_Stream3_IRQn, DMA2_Stream4_IRQn, DMA2_Stream5_IRQn, DMA2_Stream6_IRQn, DMA2_Stream7_IRQn },
    };

#if !defined(DMA_REQUEST_DCMI)
    constexpr std::array dmaChannelSelections{ DMA_CHANNEL_0, DMA_CHANNEL_1, DMA_CHANNEL_2, DMA_CHANNEL_3, DMA_CHANNEL_4, DMA_CHANNEL_5, DMA_CHANNEL_6, DMA_CHANNEL_7 };
#endif

    uint8_t DmaLine(const hal::DmaChannelId& id)
    {
        return id.stream;
    }
#else
    const std::array dmaChannels{
        std::array{ GPDMA1_Channel0, GPDMA1_Channel1, GPDMA1_Channel2, GPDMA1_Channel3, GPDMA1_Channel4, GPDMA1_Channel5, GPDMA1_Channel6, GPDMA1_Channel7 },
        std::array{ GPDMA2_Channel0, GPDMA2_Channel1, GPDMA2_Channel2, GPDMA2_Channel3, GPDMA2_Channel4, GPDMA2_Channel5, GPDMA2_Channel6, GPDMA2_Channel7 },
    };

    const std::array dmaIrqs{
        std::array{ GPDMA1_Channel0_IRQn, GPDMA1_Channel1_IRQn, GPDMA1_Channel2_IRQn, GPDMA1_Channel3_IRQn, GPDMA1_Channel4_IRQn, GPDMA1_Channel5_IRQn, GPDMA1_Channel6_IRQn, GPDMA1_Channel7_IRQn },
        std::array{ GPDMA2_Channel0_IRQn, GPDMA2_Channel1_IRQn, GPDMA2_Channel2_IRQn, GPDMA2_Channel3_IRQn, GPDMA2_Channel4_IRQn, GPDMA2_Channel5_IRQn, GPDMA2_Channel6_IRQn, GPDMA2_Channel7_IRQn },
    };

    static_assert(GPDMA1_REQUEST_DCMI == GPDMA2_REQUEST_DCMI);

    uint8_t DmaLine(const hal::DmaChannelId& id)
    {
        return id.channel;
    }
#endif

    uint32_t DataWidthValue(hal::DcmiStm::DataWidth width)
    {
        switch (width)
        {
            case hal::DcmiStm::DataWidth::bits8:
                return DCMI_EXTEND_DATA_8B;
            case hal::DcmiStm::DataWidth::bits10:
                return DCMI_EXTEND_DATA_10B;
            case hal::DcmiStm::DataWidth::bits12:
                return DCMI_EXTEND_DATA_12B;
            default:
                return DCMI_EXTEND_DATA_14B;
        }
    }

    std::size_t BytesPerPixelClock(hal::DcmiStm::DataWidth width)
    {
        return width == hal::DcmiStm::DataWidth::bits8 ? 1 : 2;
    }

    uint32_t ClockEdgeValue(hal::DcmiStm::Edge edge)
    {
        return edge == hal::DcmiStm::Edge::rising ? DCMI_PCKPOLARITY_RISING : DCMI_PCKPOLARITY_FALLING;
    }

    uint32_t VerticalBlankingValue(hal::DcmiStm::Level level)
    {
        return level == hal::DcmiStm::Level::high ? DCMI_VSPOLARITY_HIGH : DCMI_VSPOLARITY_LOW;
    }

    uint32_t HorizontalBlankingValue(hal::DcmiStm::Level level)
    {
        return level == hal::DcmiStm::Level::high ? DCMI_HSPOLARITY_HIGH : DCMI_HSPOLARITY_LOW;
    }

    IRQn_Type DmaIrq(const hal::DmaChannelId& id)
    {
        really_assert(id.dma < dmaIrqs.size() && DmaLine(id) < dmaIrqs[id.dma].size());
        return dmaIrqs[id.dma][DmaLine(id)];
    }

    uint32_t SplitFactor(uint32_t items)
    {
        uint32_t factor = 1;

        while (items / factor > maxTransferItems)
            factor *= 2;

        return factor;
    }

    bool DataCacheEnabled()
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        return (SCB->CCR & SCB_CCR_DC_Msk) != 0;
#else
        return false;
#endif
    }

    uintptr_t Address(const infra::ByteRange& range)
    {
        return reinterpret_cast<uintptr_t>(range.begin());
    }
}

namespace hal
{
    DcmiStm::DcmiStm(MultiGpioPinStm& pins, const Config& config)
        : pins{ pins, PinConfigTypeStm::dcmi, 0 }
        , config{ config }
    {
        static_assert(std::is_standard_layout_v<Handles>);

        handles.owner = this;
        EnableClocks();
        InitDcmi();
        RegisterCallbacks();
        RegisterInterrupts();
    }

    DcmiStm::~DcmiStm()
    {
        dcmiInterrupt.reset();
        dmaInterrupt.reset();
        wanted = false;
        ClearCallbacks();

        if (state != State::idle)
        {
            CLEAR_BIT(handles.dcmi.Instance->CR, DCMI_CR_CAPTURE);
            HAL_DCMI_Stop(&handles.dcmi);
        }

        if (dmaInitialized)
            DeInitDma();

        HAL_DCMI_DeInit(&handles.dcmi);
#if defined(__HAL_RCC_DCMI_PSSI_FORCE_RESET)
        __HAL_RCC_DCMI_PSSI_FORCE_RESET();
        __HAL_RCC_DCMI_PSSI_RELEASE_RESET();
#else
        __HAL_RCC_DCMI_FORCE_RESET();
        __HAL_RCC_DCMI_RELEASE_RESET();
#endif
        DisableClockDcmi(0);
    }

    void DcmiStm::Start(CameraFormat format, Mode mode, infra::ByteRange buffer, const infra::Function<void(Frame frame)>& onFrame, const infra::Function<void(Error error)>& onError)
    {
        really_assert(!wanted);
        AssertRequest(format, mode, buffer);

        this->format = format;
        this->mode = mode;
        this->buffer = buffer;
        this->onFrame = onFrame;
        this->onError = onError;
        wanted = true;

        if (state == State::idle)
            Begin();
    }

    void DcmiStm::Stop()
    {
        wanted = false;
        ClearCallbacks();

        if (state == State::active)
            BeginWindDown();
    }

    void DcmiStm::FrameEvent(DCMI_HandleTypeDef* handle)
    {
        reinterpret_cast<Handles*>(handle)->owner->OnFrame();
    }

    void DcmiStm::ErrorEvent(DCMI_HandleTypeDef* handle)
    {
        reinterpret_cast<Handles*>(handle)->owner->OnError();
    }

    void DcmiStm::EnableClocks() const
    {
        EnableClockDcmi(0);
#if defined(DMA_STREAM_BASED)
        __HAL_RCC_DMA1_CLK_ENABLE();
        __HAL_RCC_DMA2_CLK_ENABLE();
#if defined(__HAL_RCC_DMAMUX1_CLK_ENABLE)
        __HAL_RCC_DMAMUX1_CLK_ENABLE();
#endif
#else
        __HAL_RCC_GPDMA1_CLK_ENABLE();
        __HAL_RCC_GPDMA2_CLK_ENABLE();
#endif
    }

    void DcmiStm::InitDcmi()
    {
        auto& init = handles.dcmi.Init;

        handles.dcmi.Instance = peripheralDcmi[0];
        init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
        init.PCKPolarity = ClockEdgeValue(config.pixelClockEdge);
        init.VSPolarity = VerticalBlankingValue(config.verticalBlankingLevel);
        init.HSPolarity = HorizontalBlankingValue(config.horizontalBlankingLevel);
        init.CaptureRate = DCMI_CR_ALL_FRAME;
        init.ExtendedDataMode = DataWidthValue(config.dataWidth);
        init.JPEGMode = config.jpeg ? DCMI_JPEG_ENABLE : DCMI_JPEG_DISABLE;
#if defined(DCMI_CR_BSM)
        init.ByteSelectMode = DCMI_BSM_ALL;
        init.ByteSelectStart = DCMI_OEBS_ODD;
        init.LineSelectMode = DCMI_LSM_ALL;
        init.LineSelectStart = DCMI_OELS_ODD;
#endif

        auto status = HAL_DCMI_Init(&handles.dcmi);
        really_assert(status == HAL_OK);
    }

    void DcmiStm::RegisterCallbacks()
    {
        auto frameStatus = HAL_DCMI_RegisterCallback(&handles.dcmi, HAL_DCMI_FRAME_EVENT_CB_ID, &DcmiStm::FrameEvent);
        auto errorStatus = HAL_DCMI_RegisterCallback(&handles.dcmi, HAL_DCMI_ERROR_CB_ID, &DcmiStm::ErrorEvent);
        really_assert(frameStatus == HAL_OK && errorStatus == HAL_OK);
    }

    void DcmiStm::RegisterInterrupts()
    {
        dcmiInterrupt.emplace(peripheralDcmiIrq[0], config.priority, [this]
            {
                HAL_DCMI_IRQHandler(&handles.dcmi);
            });
        dmaInterrupt.emplace(DmaIrq(config.dma), config.priority, [this]
            {
                if (dmaInitialized)
                    HAL_DMA_IRQHandler(&handles.dma);
            });
    }

#if defined(DMA_STREAM_BASED)
    void DcmiStm::InitDma()
    {
        auto& dma = handles.dma;

        if (dmaInitialized)
            HAL_DMA_DeInit(&dma);

        dma.Instance = dmaChannels[config.dma.dma][config.dma.stream];
#if defined(DMA_REQUEST_DCMI)
        dma.Init.Request = DMA_REQUEST_DCMI;
#else
        really_assert(config.dma.dma == 1 && (config.dma.stream == 1 || config.dma.stream == 7) && config.dma.channel == 1);
        dma.Init.Channel = dmaChannelSelections[config.dma.channel];
#endif
        dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
        dma.Init.PeriphInc = DMA_PINC_DISABLE;
        dma.Init.MemInc = DMA_MINC_ENABLE;
        dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
        dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
        dma.Init.Mode = mode == Mode::snapshot ? DMA_NORMAL : DMA_CIRCULAR;
        dma.Init.Priority = DMA_PRIORITY_HIGH;
        dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        dma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
        dma.Init.MemBurst = DMA_MBURST_SINGLE;
        dma.Init.PeriphBurst = DMA_PBURST_SINGLE;

        auto status = HAL_DMA_Init(&dma);
        really_assert(status == HAL_OK);
        dmaInitialized = true;
        __HAL_LINKDMA(&handles.dcmi, DMA_Handle, dma);
    }

    void DcmiStm::DeInitDma()
    {
        HAL_DMA_DeInit(&handles.dma);
    }
#else
    void DcmiStm::InitDma()
    {
        auto& dma = handles.dma;
        auto bytes = TransferWords() * wordSize;
        auto split = SplitFactor(bytes / itemSize);
        auto circular = mode == Mode::continuous || split > 1;

        if (dmaInitialized)
            HAL_DMAEx_List_DeInit(&dma);

        dma.Instance = dmaChannels[config.dma.dma][config.dma.channel];
        dma.InitLinkedList.Priority = DMA_HIGH_PRIORITY;
        dma.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
        dma.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
        dma.InitLinkedList.TransferEventMode = DMA_TCEM_LAST_LL_ITEM_TRANSFER;
        dma.InitLinkedList.LinkedListMode = circular ? DMA_LINKEDLIST_CIRCULAR : DMA_LINKEDLIST_NORMAL;

        auto status = HAL_DMAEx_List_Init(&dma);
        really_assert(status == HAL_OK);
        dmaInitialized = true;

        BuildQueue(bytes / split, split > 1 ? 2 : 1, circular);
        status = HAL_DMAEx_List_LinkQ(&dma, &handles.queue);
        really_assert(status == HAL_OK);
        __HAL_LINKDMA(&handles.dcmi, DMA_Handle, dma);
    }

    void DcmiStm::BuildQueue(uint32_t nodeBytes, std::size_t nodeCount, bool circular)
    {
        DMA_NodeConfTypeDef node{};
        node.NodeType = DMA_GPDMA_LINEAR_NODE;
        node.Init.Request = GPDMA1_REQUEST_DCMI;
        node.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        node.Init.Direction = DMA_PERIPH_TO_MEMORY;
        node.Init.SrcInc = DMA_SINC_FIXED;
        node.Init.DestInc = DMA_DINC_INCREMENTED;
        node.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_WORD;
        node.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;
        node.Init.SrcBurstLength = 1;
        node.Init.DestBurstLength = 1;
        node.Init.Priority = DMA_HIGH_PRIORITY;
        node.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1;
        node.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        node.Init.Mode = DMA_NORMAL;
        node.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
        node.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
        node.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
        node.SrcAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&handles.dcmi.Instance->DR));
        node.DstAddress = static_cast<uint32_t>(Address(buffer));
        node.DataSize = nodeBytes;

        HAL_DMAEx_List_ResetQ(&handles.queue);

        for (std::size_t index = 0; index != nodeCount; ++index)
        {
            auto status = HAL_DMAEx_List_BuildNode(&node, &handles.nodes[index]);
            really_assert(status == HAL_OK);
            status = HAL_DMAEx_List_InsertNode_Tail(&handles.queue, &handles.nodes[index]);
            really_assert(status == HAL_OK);
        }

        if (circular)
        {
            auto status = HAL_DMAEx_List_SetCircularMode(&handles.queue);
            really_assert(status == HAL_OK);
        }
    }

    void DcmiStm::DeInitDma()
    {
        HAL_DMAEx_List_DeInit(&handles.dma);
    }
#endif

    void DcmiStm::ConfigureCrop()
    {
        if (!config.crop)
        {
            HAL_DCMI_DisableCrop(&handles.dcmi);
            return;
        }

        auto clocksPerPixel = BytesPerPixel(format.pixelFormat) / BytesPerPixelClock(config.dataWidth);
        HAL_DCMI_ConfigCrop(&handles.dcmi, config.crop->x * clocksPerPixel, config.crop->y, config.crop->width * clocksPerPixel - 1, config.crop->height - 1);
        HAL_DCMI_EnableCrop(&handles.dcmi);
    }

    void DcmiStm::AssertRequest(const CameraFormat& requestedFormat, Mode requestedMode, infra::ByteRange requestedBuffer) const
    {
        really_assert(IsValidFrameBuffer(requestedFormat, requestedBuffer.size()));
        really_assert(IsCompressed(requestedFormat.pixelFormat) == config.jpeg);
        really_assert(!config.jpeg || requestedMode == Mode::snapshot);
        really_assert(Address(requestedBuffer) % wordSize == 0);

        auto bytes = config.jpeg ? requestedBuffer.size() : FrameSizeInBytes(requestedFormat);
        auto items = static_cast<uint32_t>(bytes / itemSize);
        really_assert(bytes != 0 && bytes % wordSize == 0);
        really_assert(items % SplitFactor(items) == 0);
        really_assert(!config.jpeg || items <= maxTransferItems);
        really_assert(config.jpeg || BytesPerPixel(requestedFormat.pixelFormat) % BytesPerPixelClock(config.dataWidth) == 0);
        really_assert(!config.crop || (config.crop->width == requestedFormat.width && config.crop->height == requestedFormat.height));
        really_assert(!DataCacheEnabled() || (Address(requestedBuffer) % cacheLineSize == 0 && requestedBuffer.size() % cacheLineSize == 0));
    }

    uint32_t DcmiStm::TransferWords() const
    {
        auto bytes = config.jpeg ? buffer.size() : FrameSizeInBytes(format);
        return static_cast<uint32_t>(bytes / wordSize);
    }

    void DcmiStm::Begin()
    {
        state = State::active;
        events = 0;
        capturedBytes = 0;

        PrepareBufferForCapture();
        InitDma();
        ConfigureCrop();
        StartCapture();
    }

    void DcmiStm::StartCapture()
    {
        __HAL_DCMI_CLEAR_FLAG(&handles.dcmi, flagsToClear);
        __HAL_DCMI_DISABLE_IT(&handles.dcmi, DCMI_IT_LINE | DCMI_IT_VSYNC);
        __HAL_DCMI_ENABLE_IT(&handles.dcmi, DCMI_IT_ERR | DCMI_IT_OVR);

        auto captureMode = mode == Mode::snapshot ? DCMI_MODE_SNAPSHOT : DCMI_MODE_CONTINUOUS;
        auto status = HAL_DCMI_Start_DMA(&handles.dcmi, captureMode, static_cast<uint32_t>(Address(buffer)), TransferWords());
        really_assert(status == HAL_OK);

        if (config.jpeg)
            __HAL_DCMI_ENABLE_IT(&handles.dcmi, DCMI_IT_FRAME);
    }

    void DcmiStm::BeginWindDown()
    {
        state = State::windingDown;
        remainingStopPolls = static_cast<uint32_t>(config.stopTimeout.count());
        CLEAR_BIT(handles.dcmi.Instance->CR, DCMI_CR_CAPTURE);
        PollCaptureStopped();
    }

    void DcmiStm::PollCaptureStopped()
    {
        if (!READ_BIT(handles.dcmi.Instance->CR, DCMI_CR_CAPTURE) || remainingStopPolls == 0)
        {
            FinishWindDown();
            return;
        }

        --remainingStopPolls;
        stopTimer.Start(std::chrono::milliseconds(1), [this]
            {
                PollCaptureStopped();
            });
    }

    void DcmiStm::FinishWindDown()
    {
        HAL_DCMI_Stop(&handles.dcmi);
        state = State::idle;

        if (wanted)
            Begin();
    }

    void DcmiStm::ClearCallbacks()
    {
        onFrame = nullptr;
        onError = nullptr;
    }

    void DcmiStm::OnFrame()
    {
        capturedBytes = config.jpeg ? TransferWords() * wordSize - RemainingBytes() : TransferWords() * wordSize;
        PostEvent(frameEvent);
    }

    uint32_t DcmiStm::RemainingBytes() const
    {
#if defined(DMA_STREAM_BASED)
        return __HAL_DMA_GET_COUNTER(&handles.dma) * wordSize;
#else
        return handles.dma.Instance->CBR1 & DMA_CBR1_BNDT;
#endif
    }

    void DcmiStm::OnError()
    {
        auto code = handles.dcmi.ErrorCode;
        handles.dcmi.ErrorCode = HAL_DCMI_ERROR_NONE;

        if ((code & (HAL_DCMI_ERROR_SYNC | HAL_DCMI_ERROR_OVR | HAL_DCMI_ERROR_DMA)) == 0)
            return;

        PostEvent((code & HAL_DCMI_ERROR_SYNC) != 0 ? synchronizationEvent : overrunEvent);
    }

    void DcmiStm::PostEvent(uint8_t event)
    {
        events.fetch_or(event);

        if (!deliveryScheduled.exchange(true))
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    DeliverEvents();
                });
    }

    void DcmiStm::DeliverEvents()
    {
        deliveryScheduled = false;
        auto pending = events.exchange(0);

        if (!wanted || state != State::active)
            return;

        if ((pending & errorEvents) != 0)
            DeliverError((pending & synchronizationEvent) != 0 ? Error::synchronization : Error::overrun);
        else if ((pending & frameEvent) != 0)
            DeliverFrame();
    }

    void DcmiStm::DeliverFrame()
    {
        InvalidateBuffer();
        auto callback = onFrame;
        Frame frame{ buffer.begin(), buffer.begin() + capturedBytes.load() };

        if (mode == Mode::snapshot)
        {
            wanted = false;
            ClearCallbacks();
            BeginWindDown();
        }

        callback(frame);
    }

    void DcmiStm::DeliverError(Error error)
    {
        auto callback = onError;

        if (mode == Mode::snapshot)
        {
            wanted = false;
            ClearCallbacks();
        }

        BeginWindDown();
        callback(error);
    }

    void DcmiStm::PrepareBufferForCapture() const
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        if (DataCacheEnabled())
            SCB_CleanInvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(buffer.begin()), static_cast<int32_t>(buffer.size()));
#endif
    }

    void DcmiStm::InvalidateBuffer() const
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        if (DataCacheEnabled())
            SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(buffer.begin()), static_cast<int32_t>(buffer.size()));
#endif
    }
}

#endif
