#pragma once

#include "hal_st/stm32fxxx/DmaStm.hpp"
#include "hal_st/stm32fxxx/SpiDataSizeConfiguratorStm.hpp"
#include "hal_st/stm32fxxx/SpiMasterStm.hpp"
#include "hal_st/stm32fxxx/SpiMasterStmDma.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousSpiMasterStm.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/hil/commands/HilSpiCommands.hpp"
#include "services/peripheral/SpiMasterWithChipSelect.hpp"
#include "services/synchronous_peripheral/SynchronousSpiMasterWithChipSelect.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include <cstdint>
#include <optional>
#include <variant>

namespace validation
{
    class SpiFactoryStm
        : public services::HilSpiFactory
    {
    public:
        SpiFactoryStm(const services::HilPinNaming& naming, hal::DmaStm& dma, ResourceAllocation& resources);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, services::HilSpiHandle& handle) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

    private:
        struct Request
        {
            std::optional<HilPinId> clock;
            std::optional<HilPinId> mosi;
            std::optional<HilPinId> miso;
            std::optional<HilPinId> chipSelect;
            std::optional<HilPinId> slaveSelect;
            uint32_t baud = 1000000;
            uint32_t mode = 0;
            uint32_t bits = 8;
            bool dma = false;
            bool synchronous = false;
            bool lsb = false;
            uint32_t baudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
        };

        struct ClaimedPins
        {
            hal::GpioPin* clock = nullptr;
            hal::GpioPin* mosi = nullptr;
            hal::GpioPin* miso = nullptr;
            hal::GpioPin* chipSelect = nullptr;
            hal::GpioPin* slaveSelect = nullptr;
        };

        class TrackedSpiMaster
            : public hal::SpiMaster
        {
        public:
            TrackedSpiMaster(hal::SpiMaster& spi, const infra::Function<void()>& onIdle);

            void SendAndReceive(infra::ConstByteRange sendData, infra::ByteRange receiveData, hal::SpiAction nextAction, const infra::Function<void()>& onDone) override;
            void SetChipSelectConfigurator(hal::ChipSelectConfigurator& configurator) override;
            void SetCommunicationConfigurator(hal::CommunicationConfigurator& configurator) override;
            void ResetCommunicationConfigurator() override;

            bool Busy() const;

        private:
            hal::SpiMaster& spi;
            infra::Function<void()> onIdle;
            infra::AutoResetFunction<void()> onDone;
            bool busy = false;
        };

        template<class Config>
        static Config MakeConfig(const Request& request);

        services::HilStatus Evaluate(uint8_t index, const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Parse(const services::HilArguments& arguments, Request& request) const;
        services::HilStatus Claim(uint8_t index, const Request& request, services::HilPinOwner& pins, ClaimedPins& claimed) const;
        void Construct(uint8_t index, const Request& request, const ClaimedPins& claimed, services::HilSpiHandle& handle);
        hal::SpiMaster& ConstructAsynchronous(uint8_t index, const Request& request, const ClaimedPins& claimed);
        void TransferDone();
        void Destroy();

    private:
        const services::HilPinNaming& naming;
        hal::DmaStm& dma;
        ResourceAllocation& resources;
        uint8_t index = 0;
        std::optional<hal::DmaStm::TransmitStream> transmitStream;
        std::optional<hal::DmaStm::ReceiveStream> receiveStream;
        std::variant<std::monostate, hal::SpiMasterStm, hal::SpiMasterStmDma, hal::SynchronousSpiMasterStm> driver;
        std::optional<hal::SpiDataSizeConfiguratorStm> dataSize;
        std::optional<services::SpiMasterWithChipSelect> chipSelect;
        std::optional<services::SynchronousSpiMasterWithChipSelect> synchronousChipSelect;
        std::optional<TrackedSpiMaster> tracked;
        infra::TimerSingleShot quiesceTimer;
        infra::AutoResetFunction<void()> onClosed;
    };
}
