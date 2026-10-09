#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/interfaces/DisplayTiming.hpp"
#include "hal/interfaces/DsiHost.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include DEVICE_HEADER

#if defined(HAS_PERIPHERAL_DSIHOST)

namespace hal
{
    class DsiHostStm
        : public DsiHost
        , public DsiVideoStream
    {
    public:
        enum class ColorCoding : uint8_t
        {
            rgb565,
            rgb888
        };

        enum class VideoMode : uint8_t
        {
            nonBurstWithSyncPulses,
            nonBurstWithSyncEvents,
            burst
        };

        struct Pll
        {
            uint32_t inputClockHz;
            uint8_t inputDivider;
            uint8_t multiplier;
            uint8_t outputDivider;
        };

        struct Polarity
        {
            constexpr Polarity()
            {}

            bool hsyncActiveHigh{ true };
            bool vsyncActiveHigh{ true };
            bool dataEnableActiveHigh{ true };
        };

        struct LowPower
        {
            constexpr LowPower()
            {}

            bool commands{ true };
            bool horizontalFrontPorch{ true };
            bool horizontalBackPorch{ true };
            bool verticalActive{ true };
            bool verticalFrontPorch{ true };
            bool verticalBackPorch{ true };
            bool verticalSync{ true };
            uint8_t largestPacketSize{ 4 };
        };

        struct Video
        {
            constexpr Video()
            {}

            uint8_t virtualChannel{ 0 };
            ColorCoding colorCoding{ ColorCoding::rgb888 };
            VideoMode mode{ VideoMode::burst };
            uint16_t nullPacketSize{ 0xfff };
            Polarity polarity;
            LowPower lowPower;
        };

        struct PhyTimer
        {
            constexpr PhyTimer()
            {}

            uint8_t clockLaneHighSpeedToLowPower{ 35 };
            uint8_t clockLaneLowPowerToHighSpeed{ 35 };
            uint8_t dataLaneHighSpeedToLowPower{ 35 };
            uint8_t dataLaneLowPowerToHighSpeed{ 35 };
            uint8_t dataLaneMaxReadTime{ 0 };
            uint8_t stopWaitTime{ 10 };
        };

        struct Config
        {
            constexpr Config()
            {}

            uint8_t numberOfLanes{ 2 };
            Video video;
            PhyTimer phyTimer;
            uint8_t lowPowerReceiveFilter{ 0 };
            std::size_t maxParametersSize{ 64 };
        };

        DsiHostStm(const Pll& pll, const DisplayTiming& timing, const Config& config = Config());
        ~DsiHostStm();

        std::size_t MaxParametersSize() const override;
        void WriteDcs(uint8_t command, infra::ConstByteRange parameters, const infra::Function<void()>& onDone) override;
        void WriteGeneric(infra::ConstByteRange data, const infra::Function<void()>& onDone) override;
        void ReadDcs(uint8_t command, infra::ByteRange data, const infra::Function<void(Result)>& onDone) override;

        void Start(const infra::Function<void()>& onDone) override;
        void Stop(const infra::Function<void()>& onDone) override;

    private:
        uint32_t LaneByteClockHz(const Pll& pll) const;
        uint32_t EscapeClockDivider() const;
        uint32_t LaneByteClockCycles(uint32_t pixels, uint32_t pixelClockHz) const;
        void ConfigureHost(const Pll& pll);
        void ConfigureVideo(const DisplayTiming& timing);
        void ConfigurePhy();
        void ConfigureTimeouts();
        void ConfigureFlowControl();
        void ConfigureCommands();
        void BeginOperation(std::size_t size);
        void CompleteWrite(const infra::Function<void()>& onDone, HAL_StatusTypeDef status);
        void CompleteLater();

    private:
        Config config;
        uint32_t laneByteClockHz;
        DSI_HandleTypeDef handle{};
        bool streaming{ false };
        infra::AutoResetFunction<void()> pendingCompletion;
        infra::AutoResetFunction<void(Result)> pendingRead;
    };
}

#endif
