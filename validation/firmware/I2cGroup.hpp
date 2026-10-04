#pragma once

#include "hal_st/stm32fxxx/I2cStm.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilPendingOperation.hpp"
#include "services/hil/commands/HilSingleInstanceGroup.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/Payload.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

namespace validation
{
    enum class I2cHook : uint8_t
    {
        notFound,
        busError,
        arbitrationLost,
    };

    struct I2cBus
    {
        std::optional<HilPinId> scl;
        std::optional<HilPinId> sda;
    };

    struct I2cBusPins
    {
        hal::GpioPin* scl = nullptr;
        hal::GpioPin* sda = nullptr;
    };

    GPIO_TypeDef* GpioRegisters(HilPinId pin);
    uint32_t GpioMask(HilPinId pin);
    void PrintHex32(services::HilResponse::Line& line, uint32_t value);

    services::HilStatus ParseI2cBus(const services::HilArguments& arguments, const services::HilPinNaming& naming, I2cBus& bus);
    services::HilStatus CheckI2cBus(uint8_t index, const I2cBus& bus);
    services::HilStatus ClaimI2cBus(uint8_t index, const I2cBus& bus, services::HilPinOwner& pins, ResourceAllocation& resources, I2cBusPins& claimed);

    class I2cStmForHil
        : public hal::I2cStm
    {
    public:
        I2cStmForHil(uint8_t oneBasedIndex, hal::GpioPinStm& scl, hal::GpioPinStm& sda, const Config& config, const infra::Function<void(I2cHook hook)>& onHook);

        uint32_t Timing() const;
        uint32_t Count(I2cHook hook) const;
        void DisableInterrupts();

    protected:
        void DeviceNotFound() override;
        void BusError() override;
        void ArbitrationLost() override;

    private:
        void Report(I2cHook hook);

    private:
        I2C_TypeDef* peripheral;
        infra::Function<void(I2cHook hook)> onHook;
        std::array<uint32_t, 3> counts{};
    };

    class I2cFactoryStm
        : public services::HilInstanceFactory
    {
    public:
        I2cFactoryStm(const services::HilPinNaming& naming, ResourceAllocation& resources);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, const infra::Function<void(I2cHook hook)>& onHook);
        hal::I2cMaster& Master();
        uint32_t Timing() const;
        uint32_t KernelClock() const;

    private:
        struct Request
        {
            I2cBus bus;
            std::optional<uint32_t> timing;
            bool pullUp = false;
        };

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        void Destroy();

    private:
        const services::HilPinNaming& naming;
        ResourceAllocation& resources;
        std::optional<I2cStmForHil> driver;
        uint8_t index = 0;
        infra::AutoResetFunction<void()> onClosed;
    };

    class I2cCommands
        : public services::HilSingleInstanceGroup
    {
    public:
        I2cCommands(services::HilContext& context, I2cFactoryStm& factory);

        infra::MemoryRange<const Command> Commands() override;

    protected:
        services::HilStatus OpenInstance(uint8_t index, const services::HilArguments& arguments) override;
        void Opened(services::HilResponse::Line& line) const override;
        void CloseInstance() override;

    private:
        services::HilStatus Write(const services::HilArguments& arguments);
        services::HilStatus Read(const services::HilArguments& arguments);

        void Hook(I2cHook hook);
        void Sent(uint32_t operation, hal::Result result, uint32_t sent);
        void Received(uint32_t operation, hal::Result result);

    private:
        I2cFactoryStm& factory;
        services::HilPendingOperation pending;
        infra::ByteRange received;
        Output output = Output::hex;
        std::array<uint8_t, 1024> buffer{};
        std::array<Command, 4> commands;
    };
}
