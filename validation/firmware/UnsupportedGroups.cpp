#include "validation/firmware/UnsupportedGroups.hpp"
#include "services/hil/commands/HilUnsupportedCommands.hpp"
#include <array>

namespace validation
{
    namespace
    {
#if defined(STM32WB)
        constexpr std::array<const char*, 20> commandNames{ {
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
            "dac.open",
            "dac.set",
            "dac.close",
        } };
#elif defined(STM32G4)
        constexpr std::array<const char*, 28> commandNames{ {
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
            "aes.enc",
            "aes.dec",
            "pka.mul",
            "pka.check",
            "pka.cmp",
            "hsem.take",
            "hsem.release",
            "hsem.status",
            "hsem.lock",
            "hsem.mine",
            "flash.stack",
            "lptpwm.open",
            "lptpwm.duty",
            "lptpwm.pulse",
            "lptpwm.start",
            "lptpwm.stop",
            "lptpwm.close",
        } };
#elif defined(STM32WBA)
        constexpr std::array<const char*, 27> commandNames{ {
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
            "dac.open",
            "dac.set",
            "dac.close",
        } };
#else
#error "UnsupportedGroups needs the list of commands this MCU answers with ERR unsupported"
#endif
    }

    void CreateUnsupportedGroups(services::HilContext& context)
    {
        static services::HilUnsupportedCommands::WithMaxCommands<commandNames.size()> unsupported{ context, infra::MakeRange(commandNames) };
    }
}
