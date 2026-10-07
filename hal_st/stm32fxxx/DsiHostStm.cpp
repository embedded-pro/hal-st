#include "hal_st/stm32fxxx/DsiHostStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

#if defined(HAS_PERIPHERAL_DSIHOST)

namespace hal
{
    namespace
    {
        constexpr uint32_t escapeClockMaxHz = 15'620'000;
        constexpr std::size_t minParametersSize = 4;
        constexpr std::size_t maxReadSize = 0xffff;

        uint32_t OutputDividerCode(uint8_t outputDivider)
        {
            switch (outputDivider)
            {
                case 1:
                    return DSI_PLL_OUT_DIV1;
                case 2:
                    return DSI_PLL_OUT_DIV2;
                case 4:
                    return DSI_PLL_OUT_DIV4;
                case 8:
                    return DSI_PLL_OUT_DIV8;
                default:
                    break;
            }

            really_assert(false);
            return 0;
        }

        uint32_t ColorCodingValue(DsiHostStm::ColorCoding coding)
        {
            return coding == DsiHostStm::ColorCoding::rgb565 ? DSI_RGB565 : DSI_RGB888;
        }

        uint32_t VideoModeValue(DsiHostStm::VideoMode mode)
        {
            switch (mode)
            {
                case DsiHostStm::VideoMode::nonBurstWithSyncPulses:
                    return DSI_VID_MODE_NB_PULSES;
                case DsiHostStm::VideoMode::nonBurstWithSyncEvents:
                    return DSI_VID_MODE_NB_EVENTS;
                case DsiHostStm::VideoMode::burst:
                    return DSI_VID_MODE_BURST;
            }

            return DSI_VID_MODE_BURST;
        }

        uint32_t LowPowerFlag(bool enabled, uint32_t enableValue)
        {
            return enabled ? enableValue : 0;
        }

        DsiHost::Result ResultOf(HAL_StatusTypeDef status)
        {
            switch (status)
            {
                case HAL_OK:
                    return DsiHost::Result::success;
                case HAL_TIMEOUT:
                    return DsiHost::Result::timeout;
                default:
                    break;
            }

            return DsiHost::Result::failed;
        }
    }

    DsiHostStm::DsiHostStm(const Pll& pll, const DisplayTiming& timing, const Config& config)
        : config(config)
        , laneByteClockHz(LaneByteClockHz(pll))
    {
        really_assert(config.numberOfLanes == 1 || config.numberOfLanes == 2);
        really_assert(config.maxParametersSize >= minParametersSize);

        EnableClockDsiHost(0);

        handle.Instance = peripheralDsiHost[0];
        ConfigureHost(pll);
        ConfigureVideo(timing);
        ConfigurePhy();
        ConfigureTimeouts();
        ConfigureFlowControl();
    }

    DsiHostStm::~DsiHostStm()
    {
        HAL_DSI_DeInit(&handle);
        DisableClockDsiHost(0);
    }

    std::size_t DsiHostStm::MaxParametersSize() const
    {
        return config.maxParametersSize;
    }

    void DsiHostStm::WriteDcs(uint8_t command, infra::ConstByteRange parameters, const infra::Function<void()>& onDone)
    {
        BeginOperation(parameters.size());

        HAL_StatusTypeDef status;

        if (parameters.empty())
            status = HAL_DSI_ShortWrite(&handle, config.video.virtualChannel, DSI_DCS_SHORT_PKT_WRITE_P0, command, 0);
        else if (parameters.size() == 1)
            status = HAL_DSI_ShortWrite(&handle, config.video.virtualChannel, DSI_DCS_SHORT_PKT_WRITE_P1, command, parameters.front());
        else
            status = HAL_DSI_LongWrite(&handle, config.video.virtualChannel, DSI_DCS_LONG_PKT_WRITE, parameters.size(), command, parameters.begin());

        CompleteWrite(onDone, status);
    }

    void DsiHostStm::WriteGeneric(infra::ConstByteRange data, const infra::Function<void()>& onDone)
    {
        BeginOperation(data.size());

        HAL_StatusTypeDef status;

        if (data.empty())
            status = HAL_DSI_ShortWrite(&handle, config.video.virtualChannel, DSI_GEN_SHORT_PKT_WRITE_P0, 0, 0);
        else if (data.size() == 1)
            status = HAL_DSI_ShortWrite(&handle, config.video.virtualChannel, DSI_GEN_SHORT_PKT_WRITE_P1, data[0], 0);
        else if (data.size() == 2)
            status = HAL_DSI_ShortWrite(&handle, config.video.virtualChannel, DSI_GEN_SHORT_PKT_WRITE_P2, data[0], data[1]);
        else
            status = HAL_DSI_LongWrite(&handle, config.video.virtualChannel, DSI_GEN_LONG_PKT_WRITE, data.size() - 1, data[0], data.begin() + 1);

        CompleteWrite(onDone, status);
    }

    void DsiHostStm::ReadDcs(uint8_t command, infra::ByteRange data, const infra::Function<void(Result)>& onDone)
    {
        really_assert(!data.empty() && data.size() <= maxReadSize);
        BeginOperation(0);

        auto result = ResultOf(HAL_DSI_Read(&handle, config.video.virtualChannel, data.begin(), data.size(), DSI_DCS_SHORT_PKT_READ, command, nullptr));

        pendingRead = onDone;
        infra::EventDispatcher::Instance().Schedule([this, result]()
            {
                pendingRead(result);
            });
    }

    void DsiHostStm::Start(const infra::Function<void()>& onDone)
    {
        really_assert(!streaming);
        BeginOperation(0);

        auto result = HAL_DSI_Start(&handle);
        really_assert(result == HAL_OK);
        streaming = true;

        CompleteWrite(onDone, HAL_OK);
    }

    void DsiHostStm::Stop(const infra::Function<void()>& onDone)
    {
        really_assert(streaming);
        BeginOperation(0);

        auto result = HAL_DSI_Stop(&handle);
        really_assert(result == HAL_OK);
        streaming = false;

        CompleteWrite(onDone, HAL_OK);
    }

    uint32_t DsiHostStm::LaneByteClockHz(const Pll& pll) const
    {
        really_assert(pll.inputDivider >= 1 && pll.inputDivider <= 7);
        really_assert(pll.multiplier >= 10 && pll.multiplier <= 125);
        really_assert(pll.outputDivider == 1 || pll.outputDivider == 2 || pll.outputDivider == 4 || pll.outputDivider == 8);

        uint64_t laneBitRate = uint64_t{ pll.inputClockHz } / pll.inputDivider * pll.multiplier / pll.outputDivider;
        return static_cast<uint32_t>(laneBitRate / 8);
    }

    uint32_t DsiHostStm::LaneByteClockCycles(uint32_t pixels, uint32_t pixelClockHz) const
    {
        return static_cast<uint32_t>(uint64_t{ pixels } * laneByteClockHz / pixelClockHz);
    }

    void DsiHostStm::ConfigureHost(const Pll& pll)
    {
        DSI_PLLInitTypeDef pllInit{};
        pllInit.PLLNDIV = pll.multiplier;
        pllInit.PLLIDF = pll.inputDivider;
        pllInit.PLLODF = OutputDividerCode(pll.outputDivider);

        handle.Init.AutomaticClockLaneControl = DSI_AUTO_CLK_LANE_CTRL_DISABLE;
        handle.Init.TXEscapeCkdiv = laneByteClockHz / escapeClockMaxHz;
        handle.Init.NumberOfLanes = config.numberOfLanes == 1 ? DSI_ONE_DATA_LANE : DSI_TWO_DATA_LANES;

        auto result = HAL_DSI_Init(&handle, &pllInit);
        really_assert(result == HAL_OK);
    }

    void DsiHostStm::ConfigureVideo(const DisplayTiming& timing)
    {
        const Video& video = config.video;
        const LowPower& lowPower = video.lowPower;
        uint32_t horizontalLine = uint32_t{ timing.horizontalSync } + timing.horizontalBackPorch + timing.active.width + timing.horizontalFrontPorch;

        DSI_VidCfgTypeDef videoConfig{};
        videoConfig.VirtualChannelID = video.virtualChannel;
        videoConfig.ColorCoding = ColorCodingValue(video.colorCoding);
        videoConfig.LooselyPacked = DSI_LOOSELY_PACKED_DISABLE;
        videoConfig.Mode = VideoModeValue(video.mode);
        videoConfig.PacketSize = timing.active.width;
        videoConfig.NumberOfChunks = 0;
        videoConfig.NullPacketSize = video.nullPacketSize;
        videoConfig.HSPolarity = video.polarity.hsyncActiveHigh ? DSI_HSYNC_ACTIVE_HIGH : DSI_HSYNC_ACTIVE_LOW;
        videoConfig.VSPolarity = video.polarity.vsyncActiveHigh ? DSI_VSYNC_ACTIVE_HIGH : DSI_VSYNC_ACTIVE_LOW;
        videoConfig.DEPolarity = video.polarity.dataEnableActiveHigh ? DSI_DATA_ENABLE_ACTIVE_HIGH : DSI_DATA_ENABLE_ACTIVE_LOW;
        videoConfig.HorizontalSyncActive = LaneByteClockCycles(timing.horizontalSync, timing.pixelClockHz);
        videoConfig.HorizontalBackPorch = LaneByteClockCycles(timing.horizontalBackPorch, timing.pixelClockHz);
        videoConfig.HorizontalLine = LaneByteClockCycles(horizontalLine, timing.pixelClockHz);
        videoConfig.VerticalSyncActive = timing.verticalSync;
        videoConfig.VerticalBackPorch = timing.verticalBackPorch;
        videoConfig.VerticalFrontPorch = timing.verticalFrontPorch;
        videoConfig.VerticalActive = timing.active.height;
        videoConfig.LPCommandEnable = LowPowerFlag(lowPower.commands, DSI_LP_COMMAND_ENABLE);
        videoConfig.LPLargestPacketSize = lowPower.largestPacketSize;
        videoConfig.LPVACTLargestPacketSize = lowPower.largestPacketSize;
        videoConfig.LPHorizontalFrontPorchEnable = LowPowerFlag(lowPower.horizontalFrontPorch, DSI_LP_HFP_ENABLE);
        videoConfig.LPHorizontalBackPorchEnable = LowPowerFlag(lowPower.horizontalBackPorch, DSI_LP_HBP_ENABLE);
        videoConfig.LPVerticalActiveEnable = LowPowerFlag(lowPower.verticalActive, DSI_LP_VACT_ENABLE);
        videoConfig.LPVerticalFrontPorchEnable = LowPowerFlag(lowPower.verticalFrontPorch, DSI_LP_VFP_ENABLE);
        videoConfig.LPVerticalBackPorchEnable = LowPowerFlag(lowPower.verticalBackPorch, DSI_LP_VBP_ENABLE);
        videoConfig.LPVerticalSyncActiveEnable = LowPowerFlag(lowPower.verticalSync, DSI_LP_VSYNC_ENABLE);
        videoConfig.FrameBTAAcknowledgeEnable = DSI_FBTAA_DISABLE;

        auto result = HAL_DSI_ConfigVideoMode(&handle, &videoConfig);
        really_assert(result == HAL_OK);
    }

    void DsiHostStm::ConfigurePhy()
    {
        DSI_PHY_TimerTypeDef phyTimer{};
        phyTimer.ClockLaneHS2LPTime = config.phyTimer.clockLaneHighSpeedToLowPower;
        phyTimer.ClockLaneLP2HSTime = config.phyTimer.clockLaneLowPowerToHighSpeed;
        phyTimer.DataLaneHS2LPTime = config.phyTimer.dataLaneHighSpeedToLowPower;
        phyTimer.DataLaneLP2HSTime = config.phyTimer.dataLaneLowPowerToHighSpeed;
        phyTimer.DataLaneMaxReadTime = config.phyTimer.dataLaneMaxReadTime;
        phyTimer.StopWaitTime = config.phyTimer.stopWaitTime;

        auto result = HAL_DSI_ConfigPhyTimer(&handle, &phyTimer);
        really_assert(result == HAL_OK);
    }

    void DsiHostStm::ConfigureTimeouts()
    {
        // As in the ST board support packages: a timeout of zero disables that timeout
        DSI_HOST_TimeoutTypeDef timeouts{};
        timeouts.TimeoutCkdiv = 1;

        auto result = HAL_DSI_ConfigHostTimeouts(&handle, &timeouts);
        really_assert(result == HAL_OK);

        result = HAL_DSI_SetLowPowerRXFilter(&handle, config.lowPowerReceiveFilterHz);
        really_assert(result == HAL_OK);

        result = HAL_DSI_ConfigErrorMonitor(&handle, HAL_DSI_ERROR_NONE);
        really_assert(result == HAL_OK);
    }

    void DsiHostStm::ConfigureFlowControl()
    {
        // Bus turn-around lets the host read the response of the panel
        auto result = HAL_DSI_ConfigFlowControl(&handle, DSI_FLOW_CONTROL_BTA);
        really_assert(result == HAL_OK);
    }

    void DsiHostStm::BeginOperation(std::size_t size)
    {
        really_assert(!pendingCompletion && !pendingRead);
        really_assert(size <= config.maxParametersSize);
    }

    void DsiHostStm::CompleteWrite(const infra::Function<void()>& onDone, HAL_StatusTypeDef status)
    {
        really_assert(status == HAL_OK);

        pendingCompletion = onDone;
        CompleteLater();
    }

    void DsiHostStm::CompleteLater()
    {
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                pendingCompletion();
            });
    }
}

#endif
