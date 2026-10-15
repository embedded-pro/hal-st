#pragma once

#include "generated/stm32fxxx/PeripheralTable.hpp"

#if defined(HAS_PERIPHERAL_DAC)

#include "BoardProfile.hpp"
#include "hal_st/stm32fxxx/DigitalToAnalogPinStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinPool.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    class DacCommands
        : public services::TerminalCommands
    {
    public:
        explicit DacCommands(services::HilContext& context);

        infra::MemoryRange<const Command> Commands() override;

    private:
        struct Output
        {
            Output(services::HilPinId id, uint8_t dacIndex, hal::GpioPinStm& pin);

            services::HilPinId id;
            hal::DacStm dac;
            hal::DigitalToAnalogPinImplStm output;
        };

        services::HilStatus Open(const services::HilArguments& arguments);
        services::HilStatus Set(const services::HilArguments& arguments);
        services::HilStatus Close(const services::HilArguments& arguments);

        services::HilStatus ParseOutput(const services::HilArguments& arguments, services::HilPinId& id, std::optional<std::size_t>& slot) const;

    private:
        services::HilContext& context;
        services::HilPinOwner pins;
        std::array<std::optional<Output>, board::dacOutputs.size()> outputs;
        std::array<Command, 3> commands;
    };
}

#endif
