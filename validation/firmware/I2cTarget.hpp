#pragma once

#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Crc.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilSingleInstanceGroup.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/I2cGroup.hpp"
#include "validation/firmware/Payload.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/Stopwatch.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include DEVICE_HEADER

namespace validation
{
    struct I2cTargetSettings
    {
        uint32_t nack = 0;
        bool addressNack = false;
        uint32_t stretchUs = 0;
        uint32_t stretchAt = 0;
        bool fault = false;
        uint32_t faultAt = 1;
        Pattern pattern = Pattern::inc;
        uint32_t seed = 0;
    };

    // Validation scaffolding: hal-st has no I2C slave driver, so the other end of the bus is driven with LL
    class I2cTarget
    {
    public:
        static constexpr std::size_t registerCount = 256;

        struct Config
        {
            uint8_t address;
            bool sink;
            uint32_t timing;
        };

        I2cTarget(uint8_t oneBasedIndex, hal::GpioPinStm& scl, hal::GpioPinStm& sda, HilPinId sclId, HilPinId sdaId, const Config& config);
        I2cTarget(const I2cTarget& other) = delete;
        I2cTarget& operator=(const I2cTarget& other) = delete;
        ~I2cTarget();

        void Configure(const I2cTargetSettings& settings);
        void PrintStatus(services::HilResponse::Line& line) const;
        void ClearStatus();
        void Preload(std::size_t offset, infra::ConstByteRange data);
        infra::ConstByteRange Registers(std::size_t offset, std::size_t size) const;

    private:
        void EventInterrupt();
        void ErrorInterrupt();

        void Addressed();
        void ByteReceived();
        void LoadByte();
        void MasterNacked();
        void Stopped();
        void Fault();
        void Hold(uint32_t us);
        uint8_t NextPattern();

    private:
        uint8_t instance;
        I2C_TypeDef* peripheral;
        HilPinId sclId;
        HilPinId sdaId;
        hal::PeripheralPinStm scl;
        hal::PeripheralPinStm sda;
        I2C_HandleTypeDef handle{};
        bool sink;

        I2cTargetSettings settings;
        bool faultArmed = false;
        Stopwatch stopwatch;

        std::array<uint8_t, registerCount> registers{};
        uint8_t pointer = 0;
        uint32_t position = 0;
        uint32_t patternState = 0;

        std::atomic<uint32_t> received{ 0 };
        std::atomic<uint32_t> transmitted{ 0 };
        std::atomic<uint32_t> writes{ 0 };
        std::atomic<uint32_t> reads{ 0 };
        std::atomic<uint32_t> stops{ 0 };
        std::atomic<uint32_t> nacked{ 0 };
        std::atomic<uint32_t> errors{ 0 };
        infra::Crc32 crc;
        std::array<uint8_t, 32> last{};
        std::atomic<uint32_t> lastSize{ 0 };

        hal::cortex::ImmediateInterruptHandler eventHandler;
        hal::cortex::ImmediateInterruptHandler errorHandler;
    };

    class I2cTargetFactory
        : public services::HilInstanceFactory
    {
    public:
        I2cTargetFactory(const services::HilPinNaming& naming, ResourceAllocation& resources);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins);
        I2cTarget& Target();
        uint8_t Address() const;

    private:
        struct Request
        {
            I2cBus bus;
            I2cTarget::Config config{ 0x42, false, 0 };
        };

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;

    private:
        const services::HilPinNaming& naming;
        ResourceAllocation& resources;
        std::optional<I2cTarget> target;
        uint8_t index = 0;
        uint8_t address = 0;
    };

    class I2cTargetCommands
        : public services::HilSingleInstanceGroup
    {
    public:
        I2cTargetCommands(services::HilContext& context, I2cTargetFactory& factory);

        infra::MemoryRange<const Command> Commands() override;

    protected:
        services::HilStatus OpenInstance(uint8_t index, const services::HilArguments& arguments) override;
        void Opened(services::HilResponse::Line& line) const override;
        void CloseInstance() override;

    private:
        services::HilStatus Configure(const services::HilArguments& arguments);
        services::HilStatus Status(const services::HilArguments& arguments);
        services::HilStatus Preload(const services::HilArguments& arguments);
        services::HilStatus Dump(const services::HilArguments& arguments);

    private:
        I2cTargetFactory& factory;
        std::array<Command, 6> commands;
    };
}
