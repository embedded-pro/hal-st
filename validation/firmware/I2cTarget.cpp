#include "validation/firmware/I2cTarget.hpp"
#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "validation/firmware/I2cTiming.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <algorithm>
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
        constexpr uint32_t minimumAddress = 0x08;
        constexpr uint32_t maximumAddress = 0x77;
        constexpr uint32_t defaultBus = 400000;
        constexpr uint32_t maximumStretchUs = 10000;
        constexpr uint32_t maximumPosition = 0xffff;
        constexpr uint32_t faultTimeoutUs = 1000;
        constexpr uint32_t maximumDump = 128;

        constexpr std::array<const char*, 5> openKeys{ { "scl", "sda", "addr", "mode", "timing" } };
        constexpr std::array<const char*, 8> configureKeys{ { "nack", "addrnack", "stretch", "stretchat", "fault", "faultat", "pattern", "seed" } };

        constexpr std::array<HilChoice<bool>, 2> modeChoices{ {
            { "regs", false },
            { "sink", true },
        } };

        constexpr std::array<HilChoice<bool>, 2> faultChoices{ {
            { "none", false },
            { "stop", true },
        } };
    }

    I2cTarget::I2cTarget(uint8_t oneBasedIndex, hal::GpioPinStm& scl, hal::GpioPinStm& sda, HilPinId sclId, HilPinId sdaId, const Config& config)
        : instance(oneBasedIndex - 1)
        , peripheral(hal::peripheralI2c[instance])
        , sclId(sclId)
        , sdaId(sdaId)
        , scl(scl, hal::PinConfigTypeStm::i2cScl, oneBasedIndex)
        , sda(sda, hal::PinConfigTypeStm::i2cSda, oneBasedIndex)
        , sink(config.sink)
        , eventHandler(hal::peripheralI2cEvIrq[instance], [this]()
              {
                  EventInterrupt();
              })
        , errorHandler(hal::peripheralI2cErIrq[instance], [this]()
              {
                  ErrorInterrupt();
              })
    {
        hal::EnableClockI2c(instance);

        handle.Instance = peripheral;
        handle.Init.Timing = config.timing;
        handle.Init.OwnAddress1 = static_cast<uint32_t>(config.address) << 1;
        handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
        handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
        handle.Init.OwnAddress2 = 0;
        handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
        handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
        handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
        HAL_I2C_Init(&handle);

        LL_I2C_Disable(peripheral);
        LL_I2C_EnableSlaveByteControl(peripheral);
        LL_I2C_Enable(peripheral);

        LL_I2C_EnableIT_ADDR(peripheral);
        LL_I2C_EnableIT_TC(peripheral);
        LL_I2C_EnableIT_NACK(peripheral);
        LL_I2C_EnableIT_STOP(peripheral);
        LL_I2C_EnableIT_ERR(peripheral);
    }

    I2cTarget::~I2cTarget()
    {
        LL_I2C_DisableIT_ADDR(peripheral);
        LL_I2C_DisableIT_TC(peripheral);
        LL_I2C_DisableIT_NACK(peripheral);
        LL_I2C_DisableIT_STOP(peripheral);
        LL_I2C_DisableIT_ERR(peripheral);
        LL_I2C_DisableIT_TX(peripheral);

        HAL_I2C_DeInit(&handle);
        hal::DisableClockI2c(instance);
    }

    void I2cTarget::Configure(const I2cTargetSettings& settings)
    {
        this->settings = settings;
        faultArmed = settings.fault;

        if (settings.addressNack)
            LL_I2C_DisableOwnAddress1(peripheral);
        else
            LL_I2C_EnableOwnAddress1(peripheral);
    }

    void I2cTarget::PrintStatus(services::HilResponse::Line& line) const
    {
        line << " rx=" << received.load() << " tx=" << transmitted.load() << " crc=";
        PrintHex32(line, crc.Result());
        line << " writes=" << writes.load() << " reads=" << reads.load() << " stops=" << stops.load() << " nacked=" << nacked.load() << " errors=" << errors.load() << " last=";
        line.Hex(infra::Head(infra::MakeRange(last), std::min<std::size_t>(lastSize.load(), last.size())));
    }

    void I2cTarget::ClearStatus()
    {
        received = 0;
        transmitted = 0;
        writes = 0;
        reads = 0;
        stops = 0;
        nacked = 0;
        errors = 0;
        crc.Reset();
        lastSize = 0;
    }

    void I2cTarget::Preload(std::size_t offset, infra::ConstByteRange data)
    {
        std::copy(data.begin(), data.end(), registers.begin() + offset);
    }

    infra::ConstByteRange I2cTarget::Registers(std::size_t offset, std::size_t size) const
    {
        return infra::Head(infra::DiscardHead(infra::MakeRange(registers), offset), size);
    }

    void I2cTarget::EventInterrupt()
    {
        if (LL_I2C_IsActiveFlag_NACK(peripheral))
            MasterNacked();

        if (LL_I2C_IsActiveFlag_STOP(peripheral))
            Stopped();

        if (LL_I2C_IsActiveFlag_ADDR(peripheral))
            Addressed();

        if (LL_I2C_IsActiveFlag_TCR(peripheral))
            ByteReceived();

        if (LL_I2C_IsEnabledIT_TX(peripheral) && LL_I2C_IsActiveFlag_TXIS(peripheral))
            LoadByte();
    }

    void I2cTarget::ErrorInterrupt()
    {
        if (LL_I2C_IsActiveFlag_BERR(peripheral))
        {
            LL_I2C_ClearFlag_BERR(peripheral);
            ++errors;
        }

        if (LL_I2C_IsActiveFlag_ARLO(peripheral))
        {
            LL_I2C_ClearFlag_ARLO(peripheral);
            ++errors;
        }

        if (LL_I2C_IsActiveFlag_OVR(peripheral))
        {
            LL_I2C_ClearFlag_OVR(peripheral);
            ++errors;
        }
    }

    void I2cTarget::Addressed()
    {
        position = 0;

        if (settings.stretchUs != 0 && settings.stretchAt == 0)
            Hold(settings.stretchUs);

        if (LL_I2C_GetTransferDirection(peripheral) == LL_I2C_DIRECTION_WRITE)
        {
            ++writes;
            lastSize = 0;
            LL_I2C_DisableIT_TX(peripheral);
            LL_I2C_EnableReloadMode(peripheral);
            LL_I2C_SetTransferSize(peripheral, 1);
        }
        else
        {
            ++reads;
            patternState = settings.seed;
            LL_I2C_DisableReloadMode(peripheral);
            LL_I2C_ClearFlag_TXE(peripheral);
            LL_I2C_EnableIT_TX(peripheral);
        }

        LL_I2C_ClearFlag_ADDR(peripheral);
    }

    void I2cTarget::ByteReceived()
    {
        const uint8_t byte = LL_I2C_ReceiveData8(peripheral);
        ++position;

        if (position == settings.nack)
        {
            ++nacked;
            LL_I2C_AcknowledgeNextData(peripheral, LL_I2C_NACK);
        }
        else
        {
            ++received;
            crc.Update(byte);

            const auto size = lastSize.load();
            if (size < last.size())
            {
                last[size] = byte;
                lastSize = size + 1;
            }

            if (!sink && position == 1)
                pointer = byte;
            else if (!sink)
                registers[pointer++] = byte;
        }

        if (settings.stretchUs != 0 && settings.stretchAt == position)
            Hold(settings.stretchUs);

        LL_I2C_SetTransferSize(peripheral, 1);
    }

    void I2cTarget::LoadByte()
    {
        ++position;

        if (faultArmed && position == settings.faultAt)
        {
            Fault();
            return;
        }

        if (settings.stretchUs != 0 && settings.stretchAt == position)
            Hold(settings.stretchUs);

        LL_I2C_TransmitData8(peripheral, sink ? NextPattern() : registers[pointer++]);
        ++transmitted;
    }

    void I2cTarget::MasterNacked()
    {
        const bool unsent = !LL_I2C_IsActiveFlag_TXE(peripheral);

        LL_I2C_ClearFlag_NACK(peripheral);
        LL_I2C_ClearFlag_TXE(peripheral);
        LL_I2C_DisableIT_TX(peripheral);

        // The byte loaded for the next TXIS never left TXDR
        if (unsent)
        {
            --transmitted;
            if (!sink)
                --pointer;
        }
    }

    void I2cTarget::Stopped()
    {
        LL_I2C_ClearFlag_STOP(peripheral);
        LL_I2C_DisableIT_TX(peripheral);
        LL_I2C_ClearFlag_TXE(peripheral);
        ++stops;
    }

    void I2cTarget::Fault()
    {
        auto* sclPort = GpioRegisters(sclId);
        auto* sdaPort = GpioRegisters(sdaId);
        const auto sclMask = GpioMask(sclId);
        const auto sdaMask = GpioMask(sdaId);

        faultArmed = false;

        LL_GPIO_ResetOutputPin(sdaPort, sdaMask);
        LL_GPIO_ResetOutputPin(sclPort, sclMask);
        LL_GPIO_SetPinMode(sdaPort, sdaMask, LL_GPIO_MODE_OUTPUT);
        LL_GPIO_SetPinMode(sclPort, sclMask, LL_GPIO_MODE_OUTPUT);

        // SDA must rise while SCL is high to form the misplaced STOP; the master may still hold SCL low
        LL_GPIO_SetOutputPin(sclPort, sclMask);
        stopwatch.Start();
        while (!LL_GPIO_IsInputPinSet(sclPort, sclMask) && stopwatch.ElapsedUs() < faultTimeoutUs)
        {
        }
        LL_GPIO_SetOutputPin(sdaPort, sdaMask);

        LL_I2C_Disable(peripheral);
        while (LL_I2C_IsEnabled(peripheral))
        {
        }
        LL_I2C_ClearFlag_ADDR(peripheral);
        LL_I2C_ClearFlag_NACK(peripheral);
        LL_I2C_ClearFlag_STOP(peripheral);
        LL_I2C_ClearFlag_BERR(peripheral);
        LL_I2C_ClearFlag_ARLO(peripheral);
        LL_I2C_ClearFlag_OVR(peripheral);
        LL_I2C_DisableIT_TX(peripheral);
        LL_I2C_Enable(peripheral);

        LL_GPIO_SetPinMode(sclPort, sclMask, LL_GPIO_MODE_ALTERNATE);
        LL_GPIO_SetPinMode(sdaPort, sdaMask, LL_GPIO_MODE_ALTERNATE);
    }

    void I2cTarget::Hold(uint32_t us)
    {
        stopwatch.Start();
        while (stopwatch.ElapsedUs() < us)
        {
        }
    }

    uint8_t I2cTarget::NextPattern()
    {
        switch (settings.pattern)
        {
            case Pattern::inc:
                return static_cast<uint8_t>(settings.seed + position - 1);
            case Pattern::constant:
                return static_cast<uint8_t>(settings.seed);
            default:
                if (patternState == 0)
                    patternState = 1;
                patternState ^= patternState << 13;
                patternState ^= patternState >> 17;
                patternState ^= patternState << 5;
                return static_cast<uint8_t>(patternState);
        }
    }

    I2cTargetFactory::I2cTargetFactory(const services::HilPinNaming& naming, ResourceAllocation& resources)
        : naming(naming)
        , resources(resources)
    {}

    uint8_t I2cTargetFactory::Instances() const
    {
        return instances;
    }

    infra::MemoryRange<const char* const> I2cTargetFactory::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus I2cTargetFactory::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Evaluate(index, arguments, request);
    }

    void I2cTargetFactory::Close(uint8_t, const infra::Function<void()>& onClosed)
    {
        target.reset();
        resources.Release(Resource::i2c, index, owners::i2cTarget);
        onClosed();
    }

    HilStatus I2cTargetFactory::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins)
    {
        Request request;
        HilStatus status = Evaluate(index, arguments, request);
        if (status != HilStatus::done)
            return status;

        I2cBusPins claimed;
        status = ClaimI2cBus(index, request.bus, pins, resources, claimed);
        if (status != HilStatus::done)
            return status;

        this->index = index;
        address = request.config.address;
        target.emplace(index, PinOrDummy(claimed.scl), PinOrDummy(claimed.sda), *request.bus.scl, *request.bus.sda, request.config);
        return HilStatus::done;
    }

    I2cTarget& I2cTargetFactory::Target()
    {
        return *target;
    }

    uint8_t I2cTargetFactory::Address() const
    {
        return address;
    }

    HilStatus I2cTargetFactory::Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (!arguments.Has("scl") || !arguments.Has("sda"))
            return HilStatus::usage;

        uint32_t address = request.config.address;
        uint32_t timing = 0;
        HilStatus status = HilStatus::done;
        arguments.Select("mode", request.config.sink, modeChoices, status);
        arguments.Number("timing", timing, 0, std::numeric_limits<uint32_t>::max(), status);
        arguments.Number("addr", address, minimumAddress, maximumAddress, status);
        if (status != HilStatus::done)
            return status;

        if (!I2cExists(index))
            return HilStatus::range;

        request.config.address = static_cast<uint8_t>(address);
        if (arguments.Has("timing"))
            request.config.timing = timing;
        else
        {
            auto standard = I2cTiming(I2cKernelClock(index), defaultBus);
            if (!standard)
                return HilStatus::range;

            request.config.timing = *standard;
        }

        status = ParseI2cBus(arguments, naming, request.bus);
        if (status != HilStatus::done)
            return status;

        return CheckI2cBus(index, request.bus);
    }

    I2cTargetCommands::I2cTargetCommands(services::HilContext& context, I2cTargetFactory& factory)
        : services::HilSingleInstanceGroup(context, factory, owners::i2cTarget)
        , factory(factory)
        , commands{ {
              OpenCommand("i2cs.open", "<index> scl= sda= [addr=] [mode=regs|sink] [timing=]"),
              services::HilBind<I2cTargetCommands, &I2cTargetCommands::Configure>("i2cs.cfg", "<index> [nack=] [addrnack=] [stretch=] [stretchat=] [fault=none|stop] [faultat=] [pattern=] [seed=]", *this, context.response),
              services::HilBind<I2cTargetCommands, &I2cTargetCommands::Status>("i2cs.status", "<index> [clear=0|1]", *this, context.response),
              services::HilBind<I2cTargetCommands, &I2cTargetCommands::Preload>("i2cs.regs", "<index> <offset> <hex>", *this, context.response),
              services::HilBind<I2cTargetCommands, &I2cTargetCommands::Dump>("i2cs.dump", "<index> <offset> <len>", *this, context.response),
              CloseCommand("i2cs.close", "<index>"),
          } }
    {}

    infra::MemoryRange<const I2cTargetCommands::Command> I2cTargetCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus I2cTargetCommands::OpenInstance(uint8_t index, const services::HilArguments& arguments)
    {
        return factory.Open(index, arguments, Pins());
    }

    void I2cTargetCommands::Opened(services::HilResponse::Line& line) const
    {
        const uint8_t address = factory.Address();
        line << " addr=0x";
        line.Hex(infra::MakeByteRange(address));
    }

    void I2cTargetCommands::CloseInstance()
    {}

    HilStatus I2cTargetCommands::Configure(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(configureKeys)))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        if (status != HilStatus::done)
            return status;

        I2cTargetSettings settings;
        arguments.Number("nack", settings.nack, 0, maximumPosition, status);
        arguments.Flag("addrnack", settings.addressNack, status);
        arguments.Number("stretch", settings.stretchUs, 0, maximumStretchUs, status);
        arguments.Number("stretchat", settings.stretchAt, 0, maximumPosition, status);
        arguments.Select("fault", settings.fault, faultChoices, status);
        arguments.Number("faultat", settings.faultAt, 1, maximumPosition, status);
        arguments.Select("pattern", settings.pattern, patternChoices, status);
        arguments.Number("seed", settings.seed, 0, std::numeric_limits<uint32_t>::max(), status);
        if (status != HilStatus::done)
            return status;

        factory.Target().Configure(settings);
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus I2cTargetCommands::Status(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, { "clear" }))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        bool clear = false;
        arguments.Flag("clear", clear, status);
        if (status != HilStatus::done)
            return status;

        {
            auto line = Context().response.Ok();
            factory.Target().PrintStatus(line);
        }

        if (clear)
            factory.Target().ClearStatus();

        return HilStatus::done;
    }

    HilStatus I2cTargetCommands::Preload(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(3, 3, {}))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        uint32_t offset = 0;
        arguments.NumberAt(1, offset, 0, I2cTarget::registerCount - 1, status);
        if (status != HilStatus::done)
            return status;

        std::array<uint8_t, maximumDump> data;
        std::size_t size = 0;
        status = services::HilArguments::ParseHex(arguments.Positional(2), infra::MakeRange(data), size);
        if (status != HilStatus::done)
            return status;

        if (size == 0)
            return HilStatus::usage;

        if (offset + size > I2cTarget::registerCount)
            return HilStatus::range;

        factory.Target().Preload(offset, infra::Head(infra::MakeRange(data), size));
        Context().response.Ok();
        return HilStatus::done;
    }

    HilStatus I2cTargetCommands::Dump(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(3, 3, {}))
            return HilStatus::usage;

        HilStatus status = Instance().Find(arguments);
        uint32_t offset = 0;
        uint32_t length = 0;
        arguments.NumberAt(1, offset, 0, I2cTarget::registerCount - 1, status);
        arguments.NumberAt(2, length, 1, maximumDump, status);
        if (status != HilStatus::done)
            return status;

        if (offset + length > I2cTarget::registerCount)
            return HilStatus::range;

        (Context().response.Ok() << " data=").Hex(factory.Target().Registers(offset, length));
        return HilStatus::done;
    }
}
