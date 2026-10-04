#pragma once

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/SpiSlaveStmDma.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/commands/HilSingleInstanceGroup.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/Payload.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    class SpiSlaveFactory
        : public services::HilInstanceFactory
    {
    public:
        SpiSlaveFactory(const services::HilPinNaming& naming, hal::DmaStm& dma, ResourceAllocation& resources);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins);
        hal::SpiSlave& Slave();

    private:
        struct Request
        {
            std::optional<HilPinId> clock;
            std::optional<HilPinId> miso;
            std::optional<HilPinId> mosi;
            std::optional<HilPinId> slaveSelect;
        };

        struct ClaimedPins
        {
            hal::GpioPin* clock = nullptr;
            hal::GpioPin* miso = nullptr;
            hal::GpioPin* mosi = nullptr;
            hal::GpioPin* slaveSelect = nullptr;
        };

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(uint8_t index, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed);
        services::HilStatus ClaimChannel(DmaChannel channel);
        void ReleaseResources();
        void Destroy();

    private:
        const services::HilPinNaming& naming;
        hal::DmaStm& dma;
        ResourceAllocation& resources;
        uint8_t index = 0;
        std::optional<hal::DmaStm::TransmitStream> transmitStream;
        std::optional<hal::DmaStm::ReceiveStream> receiveStream;
        std::optional<hal::SpiSlaveStmDma> slave;
        infra::AutoResetFunction<void()> onClosed;
    };

    class SpiSlaveCommands
        : public services::HilSingleInstanceGroup
    {
    public:
        static constexpr std::size_t capacity = 1024;

        SpiSlaveCommands(services::HilContext& context, SpiSlaveFactory& factory);

        infra::MemoryRange<const Command> Commands() override;

    protected:
        services::HilStatus OpenInstance(uint8_t index, const services::HilArguments& arguments) override;
        void CloseInstance() override;

    private:
        services::HilStatus Arm(const services::HilArguments& arguments);
        services::HilStatus Result(const services::HilArguments& arguments);
        services::HilStatus Cancel(const services::HilArguments& arguments);

        void TransferDone();
        void Report();
        void StopWaiting();

    private:
        SpiSlaveFactory& factory;
        bool armed = false;
        bool done = false;
        bool waiting = false;
        Output output = Output::hex;
        std::size_t receiveSize = 0;
        infra::TimerSingleShot waitTimer;
        std::array<uint8_t, capacity> transmitBuffer{};
        std::array<uint8_t, capacity> receiveBuffer{};
        std::array<Command, 5> commands;
    };
}
