#include "validation/firmware/UnsupportedGroups.hpp"
#include "services/hil/commands/HilUnsupportedCommands.hpp"
#include <array>

namespace validation
{
    namespace
    {
#if defined(STM32WB)
        constexpr std::array<const char*, 17> commandNames{ {
            "comp.open",
            "comp.read",
            "comp.irq",
            "comp.count",
            "comp.close",
            "can.open",
            "can.send",
            "can.close",
            "eth.open",
            "eth.status",
            "eth.close",
            "lptpwm.open",
            "lptpwm.duty",
            "lptpwm.pulse",
            "lptpwm.start",
            "lptpwm.stop",
            "lptpwm.close",
        } };
#elif defined(STM32WBA)
        constexpr std::array<const char*, 24> commandNames{ {
            "comp.open",
            "comp.read",
            "comp.irq",
            "comp.count",
            "comp.close",
            "can.open",
            "can.send",
            "can.close",
            "eth.open",
            "eth.status",
            "eth.close",
            "clock.mco",
            "clock.hsi48",
            "hsem.take",
            "hsem.release",
            "hsem.status",
            "hsem.lock",
            "hsem.mine",
            "flash.stack",
            "qspi.open",
            "qspi.cmd",
            "qspi.poll",
            "qspi.xfer",
            "qspi.close",
        } };
#endif
    }

    void CreateUnsupportedGroups(services::HilContext& context)
    {
        static services::HilUnsupportedCommands::WithMaxCommands<commandNames.size()> unsupported{ context, infra::MakeRange(commandNames) };
    }
}
