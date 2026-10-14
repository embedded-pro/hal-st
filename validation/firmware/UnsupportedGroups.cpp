#include "validation/firmware/UnsupportedGroups.hpp"
#include "services/hil/commands/HilUnsupportedCommands.hpp"
#include <array>

namespace validation
{
    namespace
    {
#if defined(STM32WB)
        constexpr auto commandNames = std::to_array<const char*>({
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
            "cordic.calc",
            "fmac.load",
            "fmac.fir",
            "fmac.iir",
            "comp.dac",
            "opamp.open",
            "opamp.dac",
            "opamp.read",
            "opamp.close",
        });
#elif defined(STM32G4)
        constexpr auto commandNames = std::to_array<const char*>({
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
        });
#elif defined(STM32WBA)
        constexpr auto commandNames = std::to_array<const char*>({
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
            "cordic.calc",
            "fmac.load",
            "fmac.fir",
            "fmac.iir",
            "comp.dac",
            "opamp.open",
            "opamp.dac",
            "opamp.read",
            "opamp.close",
        });
#else
#error "UnsupportedGroups needs the list of commands this MCU answers with ERR unsupported"
#endif
    }

    void CreateUnsupportedGroups(services::HilContext& context)
    {
        static services::HilUnsupportedCommands::WithMaxCommands<commandNames.size()> unsupported{ context, infra::MakeRange(commandNames) };
    }
}
