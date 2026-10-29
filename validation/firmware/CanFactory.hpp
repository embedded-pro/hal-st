#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_FDCAN)

#include "hal_st/stm32fxxx/FdCanStm.hpp"
#include "services/hil/commands/HilCanCommands.hpp"
#include "validation/firmware/BoardTypes.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include <optional>

namespace validation
{
    class CanFactoryStm
        : public services::HilCanFactory
    {
    public:
        explicit CanFactoryStm(const services::HilPinNaming& naming);

        uint8_t Instances() const override;
        infra::MemoryRange<const char* const> OpenKeys() const override;
        services::HilStatus Prepare(uint8_t index, const services::HilArguments& arguments) override;
        services::HilStatus Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, const infra::Function<void(const char* error)>& onError, hal::Can*& can) override;
        void Close(uint8_t index, const infra::Function<void()>& onClosed) override;

    private:
        struct Request
        {
            std::optional<HilPinId> rx;
            std::optional<HilPinId> tx;
            uint32_t bitRate = 500000;
            hal::FdCanStm::Config config;
        };

        services::HilStatus Parse(uint8_t index, const services::HilArguments& arguments, Request& request) const;

    private:
        const services::HilPinNaming& naming;
        std::optional<hal::FdCanStm::WithMaxRxBuffer<8>> can;
        infra::Function<void(const char* error)> onError;
        infra::AutoResetFunction<void()> onClosed;
    };
}

#endif
