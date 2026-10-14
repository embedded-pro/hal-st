#include "validation/firmware/EepromGroup.hpp"
#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "validation/firmware/I2cTiming.hpp"
#include "validation/firmware/Owners.hpp"
#include "validation/firmware/PeripheralClocks.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <limits>

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint32_t instances = board::i2cInstances;
        constexpr uint32_t minimumAddress = 0x08;
        constexpr uint32_t maximumAddress = 0x77;
        constexpr uint32_t maximumSize = 65536;
        constexpr uint32_t oneByteAddressSpace = 256;
        constexpr uint32_t minimumPage = 8;
        constexpr uint32_t maximumWriteCycleMs = 20;
        constexpr uint32_t defaultBus = 400000;

        constexpr std::array<const char*, 8> attachKeys{ { "scl", "sda", "addr", "size", "page", "abytes", "freq", "wcycle" } };

        bool PowerOfTwo(uint32_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }
    }

    uint32_t EepromGroup::DetachedEeprom::Size() const
    {
        return 0;
    }

    void EepromGroup::DetachedEeprom::WriteBuffer(infra::ConstByteRange, uint32_t, infra::Function<void()> onDone)
    {
        onDone();
    }

    void EepromGroup::DetachedEeprom::ReadBuffer(infra::ByteRange, uint32_t, infra::Function<void()> onDone)
    {
        onDone();
    }

    void EepromGroup::DetachedEeprom::Erase(infra::Function<void()> onDone)
    {
        onDone();
    }

    EepromGroup::EepromGroup(services::HilContext& context, const services::HilPinNaming& naming, ResourceAllocation& resources)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , naming(naming)
        , resources(resources)
        , pins(context.pins, owners::eeprom)
        , commands{ {
              services::HilBind<EepromGroup, &EepromGroup::Attach>("eeprom.attach", "<index> scl= sda= [addr=] [size=] [page=] [abytes=1|2] [freq=] [wcycle=]", *this, context.response),
              services::HilBind<EepromGroup, &EepromGroup::Detach>("eeprom.detach", "", *this, context.response),
          } }
    {}

    infra::MemoryRange<const EepromGroup::Command> EepromGroup::Commands()
    {
        return infra::MakeRange(commands);
    }

    hal::Eeprom& EepromGroup::Instance()
    {
        if (adapter && !detaching)
            return *adapter;

        return detached;
    }

    HilStatus EepromGroup::Attach(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(1, 1, infra::MakeRange(attachKeys)))
            return HilStatus::usage;

        uint8_t requested = 0;
        Request request;
        HilStatus status = Evaluate(arguments, requested, request);
        if (status != HilStatus::done)
            return status;

        if (adapter)
            return HilStatus::busy;

        I2cBusPins claimed;
        status = ClaimI2cBus(requested, request.bus, pins, resources, claimed);
        if (status != HilStatus::done)
        {
            pins.Release();
            return status;
        }

        hal::I2cStm::Config config;
        config.timing = request.timing;

        index = requested;
        auto& master = driver.emplace(requested, PinOrDummy(claimed.scl), PinOrDummy(claimed.sda), config, nullptr);
        adapter.emplace(master, request.config, [this](I2cEepromError error, uint32_t address)
            {
                Error(error, address);
            });

        context.response.Ok();
        return HilStatus::done;
    }

    HilStatus EepromGroup::Detach(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, {}))
            return HilStatus::usage;

        if (!adapter)
            return HilStatus::notOpen;

        if (detaching || adapter->Busy())
            return HilStatus::busy;

        if (adapter->Failed())
            adapter->Recover();

        detaching = true;
        driver->DisableInterrupts();

        // I2cStm schedules its completions and its dispatched error handler with its own address: let those already
        // queued run against a live driver
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                Destroy();
                context.response.Ok();
            });

        return HilStatus::done;
    }

    HilStatus EepromGroup::Evaluate(const services::HilArguments& arguments, uint8_t& index, Request& request) const
    {
        uint32_t requested = 0;
        HilStatus status = HilStatus::done;
        arguments.NumberAt(0, requested, 0, instances - 1, status);
        if (status != HilStatus::done)
            return status;

        if (!arguments.Has("scl") || !arguments.Has("sda"))
            return HilStatus::usage;

        uint32_t address = request.config.address;
        uint32_t size = request.config.size;
        uint32_t page = request.config.page;
        uint32_t addressBytes = request.config.addressBytes;
        uint32_t writeCycleMs = request.config.writeCycleMs;
        uint32_t frequency = defaultBus;
        constexpr auto any = std::numeric_limits<uint32_t>::max();
        arguments.Number("addr", address, 0, any, status);
        arguments.Number("size", size, 0, any, status);
        arguments.Number("page", page, 0, any, status);
        arguments.Number("abytes", addressBytes, 0, any, status);
        arguments.Number("wcycle", writeCycleMs, 0, any, status);
        arguments.Number("freq", frequency, 0, any, status);
        if (status != HilStatus::done)
            return status;

        index = static_cast<uint8_t>(requested);
        if (!I2cExists(index) || address < minimumAddress || address > maximumAddress || size == 0 || size > maximumSize)
            return HilStatus::range;

        if (page < minimumPage || page > I2cEepromStm::maximumPage || !PowerOfTwo(page) || addressBytes < 1 || addressBytes > I2cEepromStm::maximumAddressBytes || writeCycleMs > maximumWriteCycleMs)
            return HilStatus::range;

        if (addressBytes == 1 && size > oneByteAddressSpace)
            return HilStatus::range;

        auto timing = I2cTiming(I2cKernelClock(index), frequency);
        if (!timing)
            return HilStatus::range;

        request.timing = *timing;
        request.config.address = static_cast<uint8_t>(address);
        request.config.size = size;
        request.config.page = page;
        request.config.addressBytes = static_cast<uint8_t>(addressBytes);
        request.config.writeCycleMs = writeCycleMs;

        status = ParseI2cBus(arguments, naming, request.bus);
        if (status != HilStatus::done)
            return status;

        return CheckI2cBus(index, request.bus);
    }

    void EepromGroup::Error(I2cEepromError error, uint32_t address)
    {
        context.response.Event("eeprom") << " error=" << (error == I2cEepromError::busError ? "buserror" : "nack") << " address=" << address;
    }

    void EepromGroup::Destroy()
    {
        adapter.reset();
        driver.reset();
        resources.Release(Resource::i2c, index, owners::eeprom);
        pins.Release();
        detaching = false;
    }
}
