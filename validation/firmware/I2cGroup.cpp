#include "validation/firmware/I2cGroup.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/Endian.hpp"
#include "validation/firmware/I2cTiming.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <chrono>
#include <limits>

#if defined(STM32WB)
#include "stm32wbxx_ll_gpio.h"
#include "stm32wbxx_ll_i2c.h"
#elif defined(STM32WBA)
#include "stm32wbaxx_ll_gpio.h"
#include "stm32wbaxx_ll_i2c.h"
#endif

namespace validation
{
    namespace
    {
        using services::HilChoice;
        using services::HilStatus;

        constexpr uint8_t instances = 4;
        constexpr uint32_t maximumAddress = 0x7f;
        constexpr infra::Duration transferTimeout = std::chrono::milliseconds(1000);

        constexpr std::array<const char*, 5> openKeys{ { "scl", "sda", "freq", "timing", "pull" } };
        constexpr std::array<const char*, 4> writeKeys{ { "next", "len", "pattern", "seed" } };
        constexpr std::array<const char*, 2> readKeys{ { "next", "out" } };

        constexpr std::array<HilChoice<hal::Action>, 3> nextChoices{ {
            { "stop", hal::Action::stop },
            { "restart", hal::Action::repeatedStart },
            { "continue", hal::Action::continueSession },
        } };

        constexpr std::array<HilChoice<bool>, 2> pullChoices{ {
            { "none", false },
            { "up", true },
        } };

        const char* ToString(hal::Result result)
        {
            switch (result)
            {
                case hal::Result::complete:
                    return "complete";
                case hal::Result::partialComplete:
                    return "nack";
                default:
                    return "buserror";
            }
        }

        const char* ToString(I2cHook hook)
        {
            switch (hook)
            {
                case I2cHook::notFound:
                    return "notfound";
                case I2cHook::busError:
                    return "buserror";
                default:
                    return "arblost";
            }
        }
    }

    GPIO_TypeDef* GpioRegisters(HilPinId pin)
    {
        const std::array ports{
#if defined(GPIOA)
            GPIOA,
#endif
#if defined(GPIOB)
            GPIOB,
#endif
#if defined(GPIOC)
            GPIOC,
#endif
#if defined(GPIOD)
            GPIOD,
#endif
#if defined(GPIOE)
            GPIOE,
#endif
#if defined(GPIOH)
            GPIOH,
#endif
        };

        return ports[pin.port];
    }

    uint32_t GpioMask(HilPinId pin)
    {
        return uint32_t{ 1 } << pin.index;
    }

    void PrintHex32(services::HilResponse::Line& line, uint32_t value)
    {
        infra::BigEndian<uint32_t> bigEndian{ value };
        line.Hex(infra::MakeByteRange(bigEndian));
    }

    HilStatus ParseI2cBus(const services::HilArguments& arguments, const services::HilPinNaming& naming, I2cBus& bus)
    {
        HilStatus status = HilStatus::done;
        arguments.Pin("scl", naming, bus.scl, status);
        arguments.Pin("sda", naming, bus.sda, status);
        return status;
    }

    HilStatus CheckI2cBus(uint8_t index, const I2cBus& bus)
    {
        if (!SupportsFunction(*bus.scl, hal::PinConfigTypeStm::i2cScl, index) || !SupportsFunction(*bus.sda, hal::PinConfigTypeStm::i2cSda, index))
            return HilStatus::pin;

        return HilStatus::done;
    }

    HilStatus ClaimI2cBus(uint8_t index, const I2cBus& bus, services::HilPinOwner& pins, ResourceAllocation& resources, I2cBusPins& claimed)
    {
        HilStatus status = resources.Claim(Resource::i2c, index, pins.Id());
        if (status != HilStatus::done)
            return status;

        status = pins.ClaimFunction(*bus.scl, Function(hal::PinConfigTypeStm::i2cScl), index, claimed.scl);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(*bus.sda, Function(hal::PinConfigTypeStm::i2cSda), index, claimed.sda);
        if (status != HilStatus::done)
            resources.Release(Resource::i2c, index, pins.Id());

        return status;
    }

    I2cStmForHil::I2cStmForHil(uint8_t oneBasedIndex, hal::GpioPinStm& scl, hal::GpioPinStm& sda, const Config& config, const infra::Function<void(I2cHook hook)>& onHook)
        : hal::I2cStm(oneBasedIndex, scl, sda, config)
        , peripheral(hal::peripheralI2c[oneBasedIndex - 1])
        , onHook(onHook)
    {}

    uint32_t I2cStmForHil::Timing() const
    {
        return (LL_I2C_GetTimingPrescaler(peripheral) << I2C_TIMINGR_PRESC_Pos) | (LL_I2C_GetDataSetupTime(peripheral) << I2C_TIMINGR_SCLDEL_Pos) | (LL_I2C_GetDataHoldTime(peripheral) << I2C_TIMINGR_SDADEL_Pos) | (LL_I2C_GetClockHighPeriod(peripheral) << I2C_TIMINGR_SCLH_Pos) | (LL_I2C_GetClockLowPeriod(peripheral) << I2C_TIMINGR_SCLL_Pos);
    }

    uint32_t I2cStmForHil::Count(I2cHook hook) const
    {
        return counts[static_cast<std::size_t>(hook)];
    }

    void I2cStmForHil::DisableInterrupts()
    {
        LL_I2C_DisableIT_TX(peripheral);
        LL_I2C_DisableIT_RX(peripheral);
        LL_I2C_DisableIT_TC(peripheral);
        LL_I2C_DisableIT_NACK(peripheral);
        LL_I2C_DisableIT_ERR(peripheral);
    }

    void I2cStmForHil::DeviceNotFound()
    {
        Report(I2cHook::notFound);
    }

    void I2cStmForHil::BusError()
    {
        Report(I2cHook::busError);
    }

    void I2cStmForHil::ArbitrationLost()
    {
        Report(I2cHook::arbitrationLost);
    }

    void I2cStmForHil::Report(I2cHook hook)
    {
        ++counts[static_cast<std::size_t>(hook)];

        if (onHook != nullptr)
            onHook(hook);
    }

    I2cFactoryStm::I2cFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources)
        : naming(naming)
        , resources(resources)
    {}

    uint8_t I2cFactoryStm::Instances() const
    {
        return instances;
    }

    infra::MemoryRange<const char* const> I2cFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus I2cFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    void I2cFactoryStm::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        this->onClosed = onClosed;

        if (driver)
            driver->DisableInterrupts();

        // I2cStm schedules its completions and its dispatched error handler with its own address: let those already
        // queued run against a live driver
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                Destroy();
            });
    }

    HilStatus I2cFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, const infra::Function<void(I2cHook hook)>& onHook)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        I2cBusPins claimed;
        status = ClaimI2cBus(index, request.bus, pins, resources, claimed);
        if (status != HilStatus::done)
            return status;

        hal::I2cStm::Config config;
        if (request.timing)
            config.timing = *request.timing;

        this->index = index;
        driver.emplace(index, PinOrDummy(claimed.scl), PinOrDummy(claimed.sda), config, onHook);

        // The pinout table configures the I2C pins open drain without pull; this lets Standard mode run without external resistors
        if (request.pullUp)
            for (auto pin : { *request.bus.scl, *request.bus.sda })
                LL_GPIO_SetPinPull(GpioRegisters(pin), GpioMask(pin), LL_GPIO_PULL_UP);

        return HilStatus::done;
    }

    hal::I2cMaster& I2cFactoryStm::Master()
    {
        return *driver;
    }

    uint32_t I2cFactoryStm::Timing() const
    {
        return driver->Timing();
    }

    uint32_t I2cFactoryStm::KernelClock() const
    {
        return I2cKernelClock(index);
    }

    HilStatus I2cFactoryStm::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (!arguments.Has("scl") || !arguments.Has("sda") || (arguments.Has("freq") && arguments.Has("timing")))
            return HilStatus::usage;

        uint32_t frequency = 0;
        uint32_t timing = 0;
        HilStatus status = HilStatus::done;
        arguments.Select("pull", request.pullUp, pullChoices, status);
        arguments.Number("timing", timing, 0, std::numeric_limits<uint32_t>::max(), status);
        arguments.Number("freq", frequency, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status != HilStatus::done)
            return status;

        if (!I2cExists(index))
            return HilStatus::range;

        if (arguments.Has("timing"))
            request.timing = timing;
        else if (arguments.Has("freq"))
        {
            request.timing = I2cTiming(I2cKernelClock(index), frequency);
            if (!request.timing)
                return HilStatus::range;
        }

        status = ParseI2cBus(arguments, naming, request.bus);
        if (status != HilStatus::done)
            return status;

        return CheckI2cBus(index, request.bus);
    }

    void I2cFactoryStm::Destroy()
    {
        driver.reset();
        resources.Release(Resource::i2c, index, owners::i2c);
        onClosed();
    }

    I2cCommands::I2cCommands(services::HilContext& context, I2cFactoryStm& factory)
        : services::HilSingleInstanceGroup(context, factory, owners::i2c)
        , factory(factory)
        , pending(context.response)
        , commands{ {
              OpenCommand("i2c.open", "<index> scl= sda= [freq=|timing=] [pull=none|up]"),
              services::HilBind<I2cCommands, &I2cCommands::Write>("i2c.write", "<index> <addr> <hex|-> [next=] [len=] [pattern=] [seed=]", *this, context.response),
              services::HilBind<I2cCommands, &I2cCommands::Read>("i2c.read", "<index> <addr> <len> [next=] [out=hex|crc]", *this, context.response),
              CloseCommand("i2c.close", "<index>"),
          } }
    {}

    infra::MemoryRange<const I2cCommands::Command> I2cCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus I2cCommands::OpenInstance(uint8_t index, const services::HilArguments& arguments)
    {
        return factory.Open(index, arguments, Pins(), [this](I2cHook hook)
            {
                Hook(hook);
            });
    }

    void I2cCommands::Opened(services::HilResponse::Line& line) const
    {
        line << " timing=0x";
        PrintHex32(line, factory.Timing());
        line << " kernel=" << factory.KernelClock();
    }

    void I2cCommands::CloseInstance()
    {
        pending.Cancel();
    }

    HilStatus I2cCommands::Write(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(3, 3, infra::MakeRange(writeKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        uint32_t address = 0;
        hal::Action action = hal::Action::stop;
        arguments.NumberAt(1, address, 0, maximumAddress, status);
        arguments.Select("next", action, nextChoices, status);
        if (status != HilStatus::done)
            return status;

        infra::ByteRange payload;
        status = ParsePayload(arguments, 2, infra::MakeRange(buffer), payload);
        if (status != HilStatus::done)
            return status;

        if (pending.Busy())
            return HilStatus::busy;

        const auto operation = pending.Start(transferTimeout);
        factory.Master().SendData(hal::I2cAddress(static_cast<uint16_t>(address)), payload, action, [this, operation](hal::Result result, uint32_t sent)
            {
                Sent(operation, result, sent);
            });

        return HilStatus::done;
    }

    HilStatus I2cCommands::Read(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(3, 3, infra::MakeRange(readKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        uint32_t address = 0;
        uint32_t length = 0;
        hal::Action action = hal::Action::stop;
        Output requested = Output::hex;
        arguments.NumberAt(1, address, 0, maximumAddress, status);
        arguments.NumberAt(2, length, 1, static_cast<uint32_t>(buffer.size()), status);
        arguments.Select("next", action, nextChoices, status);
        if (status == HilStatus::done)
            status = ParseOutput(arguments, requested);
        if (status == HilStatus::done)
            status = CheckOutput(length, requested);
        if (status != HilStatus::done)
            return status;

        if (pending.Busy())
            return HilStatus::busy;

        output = requested;
        received = infra::Head(infra::MakeRange(buffer), length);
        const auto operation = pending.Start(transferTimeout);
        factory.Master().ReceiveData(hal::I2cAddress(static_cast<uint16_t>(address)), received, action, [this, operation](hal::Result result)
            {
                Received(operation, result);
            });

        return HilStatus::done;
    }

    void I2cCommands::Hook(I2cHook hook)
    {
        if (Instance().Occupied())
            Context().response.Event("i2c") << " index=" << static_cast<uint32_t>(Instance().Index()) << " hook=" << ToString(hook);
    }

    void I2cCommands::Sent(uint32_t operation, hal::Result result, uint32_t sent)
    {
        if (pending.Complete(operation))
            Context().response.Ok() << " sent=" << sent << " result=" << ToString(result);
    }

    void I2cCommands::Received(uint32_t operation, hal::Result result)
    {
        if (!pending.Complete(operation))
            return;

        auto line = Context().response.Ok();
        line << " result=" << ToString(result);
        if (result == hal::Result::complete)
            PrintData(line, received, output);
    }
}
