#include "boards/mb1166/Mb1166Setup.hpp"
#include "demo/audio_demo/ToneDemo.hpp"
#include "demo/stm32h757i_eval/AnalogMonitor.hpp"
#include "demo/stm32h757i_eval/Dashboard.hpp"
#include "demo/stm32h757i_eval/DefaultClockEvalH757I.hpp"
#include "demo/stm32h757i_eval/DisplayReport.hpp"
#include "demo/stm32h757i_eval/EvalMemories.hpp"
#include "demo/stm32h757i_eval/EvalUi.hpp"
#include "demo/stm32h757i_eval/MemoryTests.hpp"
#include "demo/stm32h757i_eval/Mfx.hpp"
#include "demo/stm32h757i_eval/QuadSpiMemory.hpp"
#include "drivers/audio/wm8994/Wm8994.hpp"
#include "drivers/audio/wm8994/Wm8994BusAccessI2c.hpp"
#include "drivers/display/mipi_dsi/MipiDcs.hpp"
#include "drivers/touch_screen/ft6x06/Ft6x06.hpp"
#include "drivers/touch_screen/ft6x06/Ft6x06BusAccessI2c.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/AnalogToDigitalPinStm.hpp"
#include "hal_st/stm32fxxx/DigitalToAnalogPinStm.hpp"
#include "hal_st/stm32fxxx/Dma2dStm.hpp"
#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/DsiHostStm.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "hal_st/stm32fxxx/I2cStm.hpp"
#include "hal_st/stm32fxxx/LtdcStm.hpp"
#include "hal_st/stm32fxxx/NorFlashStm.hpp"
#include "hal_st/stm32fxxx/QuadSpiStm.hpp"
#include "hal_st/stm32fxxx/SaiOutputStm.hpp"
#include "hal_st/stm32fxxx/SaiStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "hal_st/stm32fxxx/SramStm.hpp"
#include "hal_st/stm32fxxx/UartStm.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/stream/StringInputStream.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/Timer.hpp"
#include "services/peripheral/DebouncedButton.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "services/peripheral/I2cMultipleAccess.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/TracerWithDateTime.hpp"
#include "services/util/Terminal.hpp"
#include "services/util/TerminalWithStorage.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>

unsigned int hse_value = 25'000'000;

namespace
{
    constexpr hal::DisplayTiming lcdTiming{ 27'500'000, boards::mb1166Panel.size, 34, 2, 34, 16, 1, 15 };
    // The ST board support package programs the DSI line period for a 27.429 MHz pixel clock, the value of an older clock set-up, and the panel is known to work with it
    constexpr hal::DisplayTiming dsiTiming{ 27'429'000, boards::mb1166Panel.size, 34, 2, 34, 16, 1, 15 };
    constexpr hal::DsiHostStm::Pll dsiPll{ 25'000'000, 5, 100, 1 };

    constexpr std::size_t sdramPatternBytes = 2 * 1024 * 1024;
    constexpr std::size_t sdramRetestBytes = 4 * 1024 * 1024;
    constexpr std::size_t norCheckBytes = 4096;
    constexpr std::size_t audioBufferSamples = 2048;
    constexpr uint8_t codecInitialVolumePercent = 70;
    constexpr uint8_t minimumVolumePercent = 10;
    constexpr int volumeHysteresisPercent = 2;
    constexpr uint16_t adcMaximum = 4095;
    constexpr infra::Duration qspiResponseTimeout = std::chrono::seconds(3);
    constexpr infra::Duration touchStartDelay = std::chrono::milliseconds(500);
    constexpr infra::Duration displayReportDelay = std::chrono::seconds(2);
    constexpr uint32_t movedEventsPerTrace = 10;

    // The portrait panel is driven landscape with the axes exchanged: x follows the controller's y and y runs against its x
    drivers::Ft6x06::Config TouchConfig()
    {
        drivers::Ft6x06::Config config;
        config.orientation = drivers::Ft6x06::Orientation{ true, false, true };
        return config;
    }

    // Display bring-up variants, kept over a software restart, to find out which part of the ST sequence the panel needs
    constexpr uint32_t bringupMagic = 0xb01d0001;
    constexpr uint32_t bringupEmilReset = 1;
    constexpr uint32_t bringupNoParameterWrites = 2;
    constexpr uint32_t bringupLineFor27500 = 4;
    constexpr uint32_t bringupPhyTimers = 8;
    constexpr uint32_t bringupHighSpeedCommands = 16;
    constexpr uint32_t bringupLateBacklight = 32;
    constexpr uint32_t bringupAll = 63;

    struct BringupStore
    {
        uint32_t magic;
        uint32_t flags;
    };

    __attribute__((section(".noinit"))) volatile BringupStore bringupStore;

    uint32_t BringupFlags()
    {
        return bringupStore.magic == bringupMagic ? bringupStore.flags : 0;
    }

    hal::DsiHostStm::Config DsiConfig(uint32_t bringup)
    {
        hal::DsiHostStm::Config config;
        config.video.colorCoding = hal::DsiHostStm::ColorCoding::rgb888;
        config.commandsInLowPower = (bringup & bringupHighSpeedCommands) == 0;

        if ((bringup & bringupPhyTimers) != 0)
            config.phyTimer = hal::DsiHostStm::PhyTimer{};

        return config;
    }

    // The expander does not answer for a while after a reset, so a missing device is reported by its driver and not by an abort
    class EvalI2c
        : public hal::I2cStm
    {
    public:
        using hal::I2cStm::I2cStm;

        uint32_t NotAcknowledged() const
        {
            return notAcknowledged;
        }

        uint32_t BusErrors() const
        {
            return busErrors;
        }

    protected:
        void DeviceNotFound() override
        {
            ++notAcknowledged;
        }

        void BusError() override
        {
            ++busErrors;
        }

        void ArbitrationLost() override
        {
            ++busErrors;
        }

    private:
        uint32_t notAcknowledged = 0;
        uint32_t busErrors = 0;
    };

    bool Parse(infra::BoundedConstString params, uint32_t& value)
    {
        infra::StringInputStream stream{ params, infra::softFail };
        stream >> value;

        return !stream.Failed();
    }

    std::size_t ParseHexBytes(infra::BoundedConstString params, infra::MemoryRange<uint8_t> bytes)
    {
        infra::StringInputStream stream{ params, infra::softFail };
        auto text = stream >> infra::hex;
        std::size_t count = 0;

        while (count != bytes.size())
        {
            uint32_t value = 0;
            text >> value;

            if (stream.Failed() || value > 0xff)
                break;

            bytes[count++] = static_cast<uint8_t>(value);
        }

        return count;
    }

    void ReportMemory(main_::Dashboard& dashboard, main_::Dashboard::Item item, const char* name, uint32_t kibibytes, std::size_t errors)
    {
        infra::StringOutputStream::WithStorage<24> detail;

        if (kibibytes >= 1024)
            detail << kibibytes / 1024 << " MB";
        else
            detail << kibibytes << " KB";

        if (errors != 0)
            detail << " " << static_cast<uint32_t>(errors) << " ERR";

        dashboard.SetStatus(item, errors == 0 ? main_::Dashboard::State::ok : main_::Dashboard::State::failed, detail.Storage());
        services::GlobalTracer().Trace() << name << " " << kibibytes << " KB: " << static_cast<uint32_t>(errors) << " errors";
    }

    std::size_t TestSdram(infra::ByteRange memory, bool boot)
    {
        if (!boot)
            return main_::TestMemory<uint32_t>(infra::Tail(memory, sdramRetestBytes), sdramRetestBytes);

        auto* tail = reinterpret_cast<volatile uint32_t*>(infra::Tail(memory, sdramPatternBytes).begin());

        return main_::TestMemory<uint32_t>(memory, sdramPatternBytes) + main_::TestPattern(tail, sdramPatternBytes / sizeof(uint32_t));
    }

    class TouchMonitor
    {
    public:
        TouchMonitor(hal::I2cMaster& i2c, main_::Dashboard& dashboard, hal::OutputPin& activityLed)
            : bus(i2c)
            , dashboard(dashboard)
            , activityLed(activityLed)
        {}

        void Begin()
        {
            touch.emplace(bus, TouchConfig(), [this](drivers::Ft6x06::InitializationResult result)
                {
                    Initialized(result);
                });
        }

    private:
        void Initialized(drivers::Ft6x06::InitializationResult result)
        {
            if (result != drivers::Ft6x06::InitializationResult::success)
            {
                dashboard.SetStatus(main_::Dashboard::Item::touch, main_::Dashboard::State::failed, "FT6X06 NOT FOUND");
                services::GlobalTracer().Trace() << "FT6x06 touch controller not found at 0x38";
                return;
            }

            infra::StringOutputStream::WithStorage<24> detail;
            detail << "FT6X06 ID " << infra::hex << static_cast<uint32_t>(touch->ChipId());
            dashboard.SetStatus(main_::Dashboard::Item::touch, main_::Dashboard::State::ok, detail.Storage());
            services::GlobalTracer().Trace() << "FT6x06 vendor " << infra::hex << static_cast<uint32_t>(touch->VendorId()) << ", chip " << static_cast<uint32_t>(touch->ChipId());

            touch->Start([this](hal::TouchScreen::Event event)
                {
                    Report(event);
                });
        }

        void Report(const hal::TouchScreen::Event& event)
        {
            dashboard.SetTouch(event.phase, event.point);
            activityLed.Set(event.phase != hal::TouchScreen::Phase::released);

            if (event.phase != hal::TouchScreen::Phase::moved)
                movedEvents = 0;
            else if (++movedEvents % movedEventsPerTrace != 0)
                return;

            services::GlobalTracer().Trace() << "touch " << PhaseName(event.phase) << " " << static_cast<uint32_t>(event.point.x) << "," << static_cast<uint32_t>(event.point.y);
        }

        static const char* PhaseName(hal::TouchScreen::Phase phase)
        {
            switch (phase)
            {
                case hal::TouchScreen::Phase::pressed:
                    return "pressed";
                case hal::TouchScreen::Phase::moved:
                    return "moved";
                case hal::TouchScreen::Phase::released:
                    return "released";
            }

            return "";
        }

    private:
        drivers::Ft6x06BusAccessI2c bus;
        main_::Dashboard& dashboard;
        hal::OutputPin& activityLed;
        std::optional<drivers::Ft6x06> touch;
        uint32_t movedEvents = 0;
    };

    class MfxMonitor
    {
    public:
        MfxMonitor(hal::I2cMaster& i2c, main_::Dashboard& dashboard, hal::OutputPin& activityLed)
            : dashboard(dashboard)
            , activityLed(activityLed)
            , mfx(i2c, main_::Mfx::Config{}, [this](bool found)
                  {
                      Initialized(found);
                  },
                  [this](uint16_t pins)
                  {
                      PinsChanged(pins);
                  })
        {}

    private:
        static constexpr uint16_t joystickSelect = 1 << 0;
        static constexpr uint16_t joystickDown = 1 << 1;
        static constexpr uint16_t joystickLeft = 1 << 2;
        static constexpr uint16_t joystickRight = 1 << 3;
        static constexpr uint16_t joystickUp = 1 << 4;
        static constexpr uint16_t joystickPins = 0x1f;
        static constexpr uint16_t sdCardDetect = 1 << 15;

        void Initialized(bool found)
        {
            if (!found)
            {
                dashboard.SetStatus(main_::Dashboard::Item::mfx, main_::Dashboard::State::failed, "NOT FOUND");
                services::GlobalTracer().Trace() << "MFX not found at 0x42";
                return;
            }

            infra::StringOutputStream::WithStorage<24> detail;
            detail << "ID " << infra::hex << static_cast<uint32_t>(mfx.Id()) << " JOYSTICK";
            dashboard.SetStatus(main_::Dashboard::Item::mfx, main_::Dashboard::State::ok, detail.Storage());
            services::GlobalTracer().Trace() << "MFX id " << infra::hex << static_cast<uint32_t>(mfx.Id());
        }

        // The joystick pulls its pin to ground
        void PinsChanged(uint16_t pins)
        {
            const uint16_t pressed = static_cast<uint16_t>(~pins & joystickPins);

            dashboard.SetButton(main_::Dashboard::Button::select, (pressed & joystickSelect) != 0);
            dashboard.SetButton(main_::Dashboard::Button::up, (pressed & joystickUp) != 0);
            dashboard.SetButton(main_::Dashboard::Button::down, (pressed & joystickDown) != 0);
            dashboard.SetButton(main_::Dashboard::Button::left, (pressed & joystickLeft) != 0);
            dashboard.SetButton(main_::Dashboard::Button::right, (pressed & joystickRight) != 0);

            if ((pressed & joystickSelect) != 0 && (previousPressed & joystickSelect) == 0)
                dashboard.ClearTouchPad();

            activityLed.Set(pressed != 0);

            services::GlobalTracer().Trace() << "mfx pins " << infra::hex << static_cast<uint32_t>(pins) << ", joystick " << static_cast<uint32_t>(pressed) << ", microSD detect " << ((pins & sdCardDetect) != 0 ? "high" : "low");
            previousPressed = pressed;
        }

    private:
        main_::Dashboard& dashboard;
        hal::OutputPin& activityLed;
        main_::Mfx mfx;
        uint16_t previousPressed{ 0 };
    };

    class StartedVideoStream
        : public hal::DsiVideoStream
    {
    public:
        void Start(const infra::Function<void()>& onDone) override
        {
            infra::EventDispatcher::Instance().Schedule(onDone);
        }

        void Stop(const infra::Function<void()>& onDone) override
        {
            infra::EventDispatcher::Instance().Schedule(onDone);
        }
    };

    class PinWithoutOutput
        : public hal::GpioPin
    {
    public:
        explicit PinWithoutOutput(hal::GpioPin& pin)
            : pin(pin)
        {}

        bool Get() const override
        {
            return pin.Get();
        }

        void Set(bool value) override
        {}

        bool GetOutputLatch() const override
        {
            return pin.GetOutputLatch();
        }

        void SetAsInput() override
        {
            pin.SetAsInput();
        }

        bool IsInput() const override
        {
            return pin.IsInput();
        }

        void Config(hal::PinConfigType config) override
        {
            pin.Config(config);
        }

        void Config(hal::PinConfigType config, bool startOutputState) override
        {
            pin.Config(config, startOutputState);
        }

        void ResetConfig() override
        {
            pin.ResetConfig();
        }

        void EnableInterrupt(const infra::Function<void()>& action, hal::InterruptTrigger trigger, hal::InterruptType type) override
        {
            pin.EnableInterrupt(action, trigger, type);
        }

        void DisableInterrupt() override
        {
            pin.DisableInterrupt();
        }

    private:
        hal::GpioPin& pin;
    };

    // ST's board support package ends the panel initialization with a no-operation and a memory write start, both as short writes with one parameter
    class MemoryWriteStart
    {
    public:
        MemoryWriteStart(hal::DsiHost& dsi, bool withParameter)
            : dsi(dsi)
            , withParameter(withParameter)
        {}

        void Start(const infra::Function<void()>& onDone)
        {
            done = onDone;

            if (!withParameter)
            {
                dsi.WriteDcs(drivers::dcs::writeMemoryStart, infra::ConstByteRange(), [this]()
                    {
                        done();
                    });
                return;
            }

            dsi.WriteDcs(noOperation, infra::MakeRange(parameter), [this]()
                {
                    dsi.WriteDcs(drivers::dcs::writeMemoryStart, infra::MakeRange(parameter), [this]()
                        {
                            done();
                        });
                });
        }

    private:
        static constexpr uint8_t noOperation = 0x00;

        hal::DsiHost& dsi;
        bool withParameter;
        infra::AutoResetFunction<void()> done;
        std::array<uint8_t, 1> parameter{};
    };

    void BusyWaitMilliseconds(uint32_t milliseconds)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        const uint32_t cycles = HAL_RCC_GetSysClockFreq() / 1000 * milliseconds;
        const uint32_t start = DWT->CYCCNT;

        while (DWT->CYCCNT - start < cycles)
        {
        }
    }

    class PanelProbe
    {
    public:
        explicit PanelProbe(hal::DsiHost& dsi)
            : dsi(dsi)
        {}

        void Run()
        {
            if (running)
                return;

            running = true;
            index = 0;
            ReadNext();
        }

    private:
        static constexpr std::array<uint8_t, 7> commands{ 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xda };

        void ReadNext()
        {
            if (index == commands.size())
            {
                running = false;
                return;
            }

            const uint8_t command = commands[index++];
            value[0] = 0;
            dsi.ReadDcs(command, infra::MakeRange(value), [this, command](hal::DsiHost::Result result)
                {
                    services::GlobalTracer().Trace() << "panel dcs " << infra::hex << static_cast<uint32_t>(command) << " = " << static_cast<uint32_t>(value[0]) << ", result " << static_cast<uint32_t>(result);
                    ReadNext();
                });
        }

    private:
        hal::DsiHost& dsi;
        std::array<uint8_t, 1> value{};
        std::size_t index{ 0 };
        bool running{ false };
    };

    class I2cScanner
    {
    public:
        explicit I2cScanner(hal::I2cMaster& i2c)
            : i2c(i2c)
        {}

        void Scan()
        {
            if (scanning)
            {
                services::GlobalTracer().Trace() << "i2c scan in progress";
                return;
            }

            scanning = true;
            found = 0;
            address = firstAddress;
            Probe();
        }

    private:
        static constexpr uint16_t firstAddress = 0x08;
        static constexpr uint16_t lastAddress = 0x77;

        void Probe()
        {
            i2c.ReceiveData(hal::I2cAddress(address), infra::MakeRange(data), hal::Action::stop, [this](hal::Result result)
                {
                    if (result == hal::Result::complete)
                    {
                        ++found;
                        services::GlobalTracer().Trace() << "i2c 0x" << infra::hex << static_cast<uint32_t>(address) << " acknowledged";
                    }

                    if (address++ != lastAddress)
                        Probe();
                    else
                    {
                        scanning = false;
                        services::GlobalTracer().Trace() << "i2c scan done, " << found << " devices";
                    }
                });
        }

    private:
        hal::I2cMaster& i2c;
        std::array<uint8_t, 1> data{};
        uint16_t address{ firstAddress };
        uint32_t found{ 0 };
        bool scanning{ false };
    };
}

int main()
{
    const bool cortexM4Stopped = hal::WaitForCortexM4Stop();

    HAL_Init();
    ConfigureDefaultClockEvalH757I();
    ConfigureLtdcClockEvalH757I();
    ConfigureAudioClockEvalH757I();

    static main_::StmEventInfrastructure eventInfrastructure;
    static main_::EvalH757Ui ui;
    static services::DebugLed debugLed(ui.ledGreen);
    static hal::OutputPin failureLed{ ui.ledRed };
    static hal::OutputPin activityLed{ ui.ledBlue };

    static hal::GpioPinStm uartTxPin{ hal::Port::B, 14 };
    static hal::GpioPinStm uartRxPin{ hal::Port::B, 15 };
    static hal::UartStm uart{ 1, uartTxPin, uartRxPin };

    // The boot report is written before the event dispatcher runs and the writer drops what does not fit
    static services::StreamWriterOnSerialCommunication::WithStorage<4096> streamWriter{ uart };
    static infra::TextOutputStream::WithErrorPolicy textOutputStream{ streamWriter };
    static services::TracerWithDateTime tracer{ textOutputStream };
    services::SetGlobalTracerInstance(tracer);

    static services::TerminalWithCommandsImpl::WithMaxQueueAndMaxHistory<> terminalWithCommands{ uart, tracer };
    static services::TerminalWithStorage::WithMaxSize<20> terminal{ terminalWithCommands, tracer };

    if (cortexM4Stopped)
        hal::ReleaseCortexM4();
    else
        tracer.Trace() << "Cortex-M4 did not enter stop mode, not released";

    tracer.Trace() << "STM32H757I-EVAL demo, Cortex-M7 at " << HAL_RCC_GetSysClockFreq() / 1000000 << " MHz";

    alignas(32) static std::array<uint8_t, main_::Dashboard::cursorBytes> cursorMemory;
    static hal::GpioPinStm lcdResetPin{ hal::Port::F, 10 };
    static hal::GpioPinStm lcdBacklightPin{ hal::Port::A, 6 };
    static hal::OutputPin lcdBacklight{ lcdBacklightPin };

    static const uint32_t bringup = BringupFlags();
    tracer.Trace() << "display bring-up variant " << bringup;

    // The ST board support package resets the panel and switches the backlight on before it brings up the DSI link; EMIL's panel driver would reset it with the clock lane already running
    if ((bringup & bringupLateBacklight) == 0)
        lcdBacklight.Set(true);

    if ((bringup & bringupEmilReset) == 0)
    {
        hal::OutputPin lcdReset{ lcdResetPin, true };
        lcdReset.Set(false);
        BusyWaitMilliseconds(20);
        lcdReset.Set(true);
        BusyWaitMilliseconds(10);
    }

    static PinWithoutOutput passiveResetPin{ lcdResetPin };
    static hal::GpioPin& panelResetPin = (bringup & bringupEmilReset) != 0 ? static_cast<hal::GpioPin&>(lcdResetPin) : passiveResetPin;

    static hal::MultiGpioPinStm fmcPins{ main_::evalFmcPins, hal::Drive::PushPull, hal::Speed::High };
    static hal::FmcStm fmc{ fmcPins };
    static hal::SdRamStm sdram{ fmc, main_::EvalSdRamConfig() };
    static hal::SramStm sram{ fmc, main_::EvalSramConfig() };

    static hal::LtdcStm ltdc{ lcdTiming, infra::MemoryRange<const hal::LtdcStm::SignalPin>() };
    static hal::Dma2dStm dma2d;
    static hal::DsiHostStm dsi{ dsiPll, (bringup & bringupLineFor27500) != 0 ? lcdTiming : dsiTiming, DsiConfig(bringup) };

    static main_::Dashboard dashboard{ ltdc, dma2d, infra::Head(sdram.Memory(), main_::Dashboard::frameBytes), infra::MakeRange(cursorMemory) };

    {
        const std::size_t errors = TestSdram(sdram.Memory(), true);
        ReportMemory(dashboard, main_::Dashboard::Item::sdram, "SDRAM", static_cast<uint32_t>(sdram.Memory().size() / 1024), errors);
    }

    {
        const std::size_t errors = main_::TestMemory<uint16_t>(sram.Memory(), main_::evalSramSize);
        ReportMemory(dashboard, main_::Dashboard::Item::sram, "SRAM", static_cast<uint32_t>(sram.Memory().size() / 1024), errors);
    }

    // PE4 to PE6 are the NOR flash address lines A20 to A22 here and carry the audio signals afterwards, so only the first 2 MB of the NOR flash answer reliably
    {
        hal::GpioPinStm a20{ hal::Port::E, 4 };
        hal::GpioPinStm a21{ hal::Port::E, 5 };
        hal::GpioPinStm a22{ hal::Port::E, 6 };
        hal::OutputPin a20Low{ a20 };
        hal::OutputPin a21Low{ a21 };
        hal::OutputPin a22Low{ a22 };

        hal::NorFlashStm nor{ fmc, main_::EvalNorConfig() };
        const auto identification = nor.ReadIdentification();

        static std::array<uint8_t, norCheckBytes> firstRead;
        static std::array<uint8_t, norCheckBytes> secondRead;
        std::copy_n(reinterpret_cast<const volatile uint8_t*>(main_::evalNorBase), firstRead.size(), firstRead.begin());
        std::copy_n(reinterpret_cast<const volatile uint8_t*>(main_::evalNorBase), secondRead.size(), secondRead.begin());

        const bool identified = identification.manufacturer != 0x0000 && identification.manufacturer != 0xffff;
        const bool stable = firstRead == secondRead;

        infra::StringOutputStream::WithStorage<24> detail;
        detail << "ID " << infra::hex << identification.manufacturer << " " << identification.device1;
        dashboard.SetStatus(main_::Dashboard::Item::nor, identified && stable ? main_::Dashboard::State::ok : main_::Dashboard::State::failed, detail.Storage());
        tracer.Trace() << "NOR first " << static_cast<uint32_t>(norCheckBytes) << " bytes read twice: " << (stable ? "stable" : "unstable") << ", manufacturer " << infra::hex << identification.manufacturer << ", device " << identification.device1 << " " << identification.device2 << " " << identification.device3;
    }

    static hal::GpioPinStm qspiClockPin{ hal::Port::B, 2 };
    static hal::GpioPinStm qspiSelectPin{ hal::Port::G, 6 };
    static hal::GpioPinStm qspiData0Pin{ hal::Port::F, 8 };
    static hal::GpioPinStm qspiData1Pin{ hal::Port::F, 9 };
    static hal::GpioPinStm qspiData2Pin{ hal::Port::F, 7 };
    static hal::GpioPinStm qspiData3Pin{ hal::Port::F, 6 };
    static hal::QuadSpiStm::Config qspiConfig = []
    {
        hal::QuadSpiStm::Config config;
        config.prescaler = 3;
        config.flashSizeLog2 = 26;
        return config;
    }();
    static hal::QuadSpiStm qspi{ qspiClockPin, qspiSelectPin, qspiData0Pin, qspiData1Pin, qspiData2Pin, qspiData3Pin, qspiConfig };
    static main_::QuadSpiMemory qspiMemory{ qspi, [](bool ok)
        {
            infra::StringOutputStream::WithStorage<24> detail;
            detail << "ID " << infra::hex << qspiMemory.JedecId();
            dashboard.SetStatus(main_::Dashboard::Item::qspi, ok ? main_::Dashboard::State::ok : main_::Dashboard::State::failed, detail.Storage());
        } };

    static infra::TimerSingleShot qspiTimeout{ qspiResponseTimeout, []
        {
            if (!qspiMemory.Ready())
            {
                dashboard.SetStatus(main_::Dashboard::Item::qspi, main_::Dashboard::State::failed, "NO RESPONSE");
                services::GlobalTracer().Trace() << "QSPI flash did not answer";
            }
        } };

    static hal::GpioPinStm i2cSclPin{ hal::Port::B, 6 };
    static hal::GpioPinStm i2cSdaPin{ hal::Port::B, 7 };
    static EvalI2c i2c{ 1, i2cSclPin, i2cSdaPin };
    static services::I2cMultipleAccessMaster i2cMaster{ i2c };
    static services::I2cMultipleAccess codecI2c{ i2cMaster };
    static services::I2cMultipleAccess touchI2c{ i2cMaster };
    static services::I2cMultipleAccess mfxI2c{ i2cMaster };
    static services::I2cMultipleAccess scanI2c{ i2cMaster };
    static I2cScanner i2cScanner{ scanI2c };

    static TouchMonitor touchMonitor{ touchI2c, dashboard, activityLed };
    static MfxMonitor mfxMonitor{ mfxI2c, dashboard, activityLed };

    static hal::GpioPinStm saiMclkPin{ hal::Port::E, 2 };
    static hal::GpioPinStm saiSckPin{ hal::Port::E, 5 };
    static hal::GpioPinStm saiFsPin{ hal::Port::E, 4 };
    static hal::GpioPinStm saiSdPin{ hal::Port::E, 6 };

    static hal::DmaStm dma;
    static hal::DmaStm::TransmitStream saiTransmitStream{ dma, hal::DmaChannelId{ 1, 0, DMA_REQUEST_SAI1_A } };
    static hal::SaiStm sai{ 1 };
    static hal::SaiOutputStm::WithBuffer<audioBufferSamples> saiOutput{ sai, saiTransmitStream, saiSdPin, saiSckPin, saiFsPin, saiMclkPin };
    static drivers::Wm8994BusAccessI2c codecBus{ codecI2c };
    static drivers::Wm8994 codec{ codecBus, saiOutput, drivers::Wm8994::Config{ drivers::Wm8994::Output::headphone, codecInitialVolumePercent } };
    static examples::ToneDemo tone{ codec };
    static bool muted = false;

    static auto startAudio = []()
    {
        tone.Start();
        dashboard.SetStatus(main_::Dashboard::Item::audio, main_::Dashboard::State::ok, "WM8994 440 HZ");
    };

    static hal::GpioPinStm dacPin{ hal::Port::A, 5 };
    static hal::DacStm dac{ 1 };
    static hal::DigitalToAnalogPinImplStm dacOutput{ dacPin, dac, hal::DigitalToAnalogPinImplStm::external };
    static hal::AdcStm adc{ 1 };
    static uint32_t lastVolumeAdjustment = 0;
    static main_::AnalogMonitor analogMonitor{ adc, dacOutput,
        [](uint16_t counts)
        {
            dashboard.SetPotentiometer(counts);

            const uint8_t volume = static_cast<uint8_t>(minimumVolumePercent + counts * (100 - minimumVolumePercent) / adcMaximum);

            if (std::abs(static_cast<int>(volume) - static_cast<int>(lastVolumeAdjustment)) >= volumeHysteresisPercent)
            {
                lastVolumeAdjustment = volume;
                codec.SetVolume(volume, []() {});
            }
        },
        [](uint16_t counts)
        {
            dashboard.SetDac(counts);
        } };
    dashboard.SetStatus(main_::Dashboard::Item::adc, main_::Dashboard::State::ok, "PA0_C POTENTIOMETER");
    dashboard.SetStatus(main_::Dashboard::Item::dac, main_::Dashboard::State::ok, "PA5 TRIANGLE");

    static services::DebouncedButton wakeupButton{ ui.buttonWakeup, []()
        {
            muted = !muted;
            codec.SetMuted(muted, []() {});
            dashboard.SetButton(main_::Dashboard::Button::wakeup, true);
            activityLed.Set(true);
            services::GlobalTracer().Trace() << "wakeup button, audio " << (muted ? "muted" : "unmuted");
        },
        []()
        {
            dashboard.SetButton(main_::Dashboard::Button::wakeup, false);
            activityLed.Set(false);
        } };

    static services::DebouncedButton tamperButton{ ui.buttonTamper, []()
        {
            dashboard.SetButton(main_::Dashboard::Button::tamper, true);
            activityLed.Set(true);

            const std::size_t errors = TestSdram(sdram.Memory(), false);
            ReportMemory(dashboard, main_::Dashboard::Item::sdram, "SDRAM retest", static_cast<uint32_t>(sdramRetestBytes / 1024), errors);
        },
        []()
        {
            dashboard.SetButton(main_::Dashboard::Button::tamper, false);
            activityLed.Set(false);
        } };

    static PanelProbe panelProbe{ dsi };
    static infra::TimerSingleShot touchStart;
    static infra::TimerSingleShot displayReport;
    static infra::TimerSingleShot restart;

    static StartedVideoStream videoStream;
    static MemoryWriteStart memoryWriteStart{ dsi, (bringup & bringupNoParameterWrites) == 0 };
    static std::optional<boards::Mb1166Setup> panel;
    static auto panelInitialized = [](drivers::MipiDsiPanelCore::InitializationResult result)
    {
        if (result == drivers::MipiDsiPanelCore::InitializationResult::success)
        {
            memoryWriteStart.Start([]()
                {
                    if ((bringup & bringupLateBacklight) != 0)
                        lcdBacklight.Set(true);

                    services::GlobalTracer().Trace() << "Display panel initialized";
                    dashboard.Start(startAudio);
                    displayReport.Start(displayReportDelay, []()
                        {
                            main_::TraceDisplayReport(dashboard.FramesShown(), dashboard.Underruns(), main_::evalSdRamBase);
                        });
                    dashboard.SetStatus(main_::Dashboard::Item::display, main_::Dashboard::State::ok, "DSI 800X480 OTM8009A");
                });
        }
        else
        {
            dashboard.SetStatus(main_::Dashboard::Item::display, main_::Dashboard::State::failed, "PANEL NOT READY");
            services::GlobalTracer().Trace() << "Display panel did not initialize";
            startAudio();
        }

        touchStart.Start(touchStartDelay, []()
            {
                touchMonitor.Begin();
            });
    };

    static auto startPanel = [](const infra::Function<void(drivers::MipiDsiPanelCore::InitializationResult)>& onInitialized)
    {
        panel.emplace(dsi, videoStream, panelResetPin, hal::PixelFormat::rgb888, onInitialized);
    };

    dsi.Start([]()
        {
            startPanel(panelInitialized);
        });

    static infra::TimerRepeating secondTimer{ std::chrono::seconds{ 1 }, []
        {
            static uint32_t seconds = 0;
            static uint32_t previousUnderruns = 0;

            dashboard.SetUptime(++seconds);

            const uint32_t underruns = static_cast<uint32_t>(dashboard.Underruns()) + tone.Underruns();
            failureLed.Set(underruns != previousUnderruns);
            previousUnderruns = underruns;

            if (seconds % 10 == 0)
                services::GlobalTracer().Trace() << "uptime " << seconds << " s, frames " << static_cast<uint32_t>(dashboard.FramesShown()) << ", display underruns " << static_cast<uint32_t>(dashboard.Underruns()) << ", audio underruns " << tone.Underruns() << ", i2c nack " << i2c.NotAcknowledged() << " errors " << i2c.BusErrors();
        } };

    terminal.AddCommand({ { "sdram", "sd", "repeat the SDRAM test on its last 4 MB" }, [](const auto& params)
        {
            const std::size_t errors = TestSdram(sdram.Memory(), false);
            ReportMemory(dashboard, main_::Dashboard::Item::sdram, "SDRAM retest", static_cast<uint32_t>(sdramRetestBytes / 1024), errors);
        } });

    terminal.AddCommand({ { "sram", "sr", "repeat the SRAM test" }, [](const auto& params)
        {
            const std::size_t errors = main_::TestMemory<uint16_t>(sram.Memory(), main_::evalSramSize);
            ReportMemory(dashboard, main_::Dashboard::Item::sram, "SRAM", static_cast<uint32_t>(sram.Memory().size() / 1024), errors);
        } });

    terminal.AddCommand({ { "qspi", "q", "read the QSPI flash identification and the start of the array twice" }, [](const auto& params)
        {
            qspiMemory.Check([](bool ok)
                {
                    services::GlobalTracer().Trace() << "QSPI check " << (ok ? "ok" : "failed");
                });
        } });

    terminal.AddCommand({ { "qspitest", "qt", "erase, program and verify the last sector of the QSPI flash" }, [](const auto& params)
        {
            qspiMemory.EraseProgramVerify([](bool ok)
                {
                    dashboard.SetStatus(main_::Dashboard::Item::qspi, ok ? main_::Dashboard::State::ok : main_::Dashboard::State::failed, ok ? "WRITE TEST OK" : "WRITE TEST FAILED");
                });
        } });

    terminal.AddCommand({ { "display", "disp", "print the DSI, LTDC, clock and frame buffer state" }, [](const auto& params)
        {
            main_::TraceDisplayReport(dashboard.FramesShown(), dashboard.Underruns(), main_::evalSdRamBase);
            panelProbe.Run();
        } });

    terminal.AddCommand({ { "panel", "p", "reset and initialize the display panel again" }, [](const auto& params)
        {
            startPanel([](drivers::MipiDsiPanelCore::InitializationResult result)
                {
                    memoryWriteStart.Start([]()
                        {
                            services::GlobalTracer().Trace() << "panel initialized again";
                        });
                });
        } });

    terminal.AddCommand({ { "i2cscan", "i2c", "list the addresses on I2C1 that acknowledge" }, [](const auto& params)
        {
            i2cScanner.Scan();
        } });

    terminal.AddCommand({ { "adc", "a", "measure PA1_C" }, [](const auto& params)
        {
            analogMonitor.MeasureInput();
        } });

    terminal.AddCommand({ { "dac", "d", "hold the DAC output on PA5, 0 to 4095", "<value>" }, [](const auto& params)
        {
            uint32_t value = 0;
            if (!Parse(params, value) || value > main_::AnalogMonitor::dacMaximum)
            {
                services::GlobalTracer().Trace() << "dac: invalid parameter";
                return;
            }

            analogMonitor.SetOutput(static_cast<uint16_t>(value));
        } });

    terminal.AddCommand({ { "wave", "w", "run the triangle wave on the DAC again" }, [](const auto& params)
        {
            analogMonitor.StartWave();
        } });

    terminal.AddCommand({ { "loopback", "loop", "sweep the DAC and read each level back on PA1_C, wire PA5 to PA1_C first" }, [](const auto& params)
        {
            analogMonitor.Loopback();
        } });

    terminal.AddCommand({ { "volume", "v", "set the codec volume in percent", "<percent>" }, [](const auto& params)
        {
            uint32_t value = 0;
            if (!Parse(params, value) || value > 100)
            {
                services::GlobalTracer().Trace() << "volume: invalid parameter";
                return;
            }

            codec.SetVolume(static_cast<uint8_t>(value), []() {});
        } });

    terminal.AddCommand({ { "mute", "m", "toggle the audio mute" }, [](const auto& params)
        {
            muted = !muted;
            codec.SetMuted(muted, []() {});
        } });

    terminal.AddCommand({ { "dcs", "dc", "send a DCS command to the panel, all values in hex: no parameter is a short write without parameter, one parameter a short write with it, more a long write", "<command> [parameters]" }, [](const auto& params)
        {
            static std::array<uint8_t, 9> bytes;
            const std::size_t count = ParseHexBytes(params, infra::MakeRange(bytes));

            if (count == 0)
            {
                services::GlobalTracer().Trace() << "dcs: <command> [parameters] in hex";
                return;
            }

            dsi.WriteDcs(bytes[0], infra::ConstByteRange(bytes.data() + 1, bytes.data() + count), []()
                {
                    services::GlobalTracer().Trace() << "dcs sent";
                });
        } });

    terminal.AddCommand({ { "bringup", "bu", "show, or select and restart with, a display bring-up variant, the sum of: 1 EMIL resets the panel with the link running, 2 final writes without parameter, 4 line period for 27.5 MHz, 8 PHY timers of 35, 16 commands in high speed, 32 backlight after the init; 0 is the ST order", "<flags>" }, [](const auto& params)
        {
            uint32_t value = 0;
            if (!Parse(params, value) || value > bringupAll)
            {
                services::GlobalTracer().Trace() << "bringup: current variant " << bringup << ", 0 to " << bringupAll;
                return;
            }

            bringupStore.magic = bringupMagic;
            bringupStore.flags = value;
            services::GlobalTracer().Trace() << "bringup: restarting with variant " << value;
            restart.Start(std::chrono::milliseconds(200), []()
                {
                    NVIC_SystemReset();
                });
        } });

    terminal.AddCommand({ { "pattern", "pt", "show a DSI host test pattern instead of the frame buffer, 0 off, 1 vertical colour bars, 2 horizontal colour bars, 3 vertical BER pattern", "<pattern>" }, [](const auto& params)
        {
            uint32_t value = 0;
            if (!Parse(params, value) || value > static_cast<uint32_t>(hal::DsiHostStm::TestPattern::verticalBerPattern))
            {
                services::GlobalTracer().Trace() << "pattern: 0 to 3";
                return;
            }

            dsi.ShowTestPattern(static_cast<hal::DsiHostStm::TestPattern>(value));
            services::GlobalTracer().Trace() << "pattern: " << value;
        } });

    services::GlobalTracer().Trace() << "type help for the commands";

    eventInfrastructure.Run();
    __builtin_unreachable();
}
