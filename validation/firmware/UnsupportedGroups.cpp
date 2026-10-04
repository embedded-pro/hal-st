#include "validation/firmware/UnsupportedGroups.hpp"
#include "services/hil/commands/HilUnsupportedCommands.hpp"
#include <array>

namespace validation
{
    namespace
    {
        constexpr std::array<const char*, 11> commandNames{ {
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
        } };
    }

    void CreateUnsupportedGroups(services::HilContext& context)
    {
        static services::HilUnsupportedCommands::WithMaxCommands<commandNames.size()> unsupported{ context, infra::MakeRange(commandNames) };
    }
}
