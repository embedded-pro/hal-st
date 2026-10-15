#include "BoardProfile.hpp"
#include "hal/cortex_m/Reset.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinNaming.hpp"
#include "services/hil/HilPinPool.hpp"
#include "services/hil/HilSystemCommands.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "validation/ble_firmware/BleGroup.hpp"
#include "validation/firmware/BoardInfoStm.hpp"
#include "validation/firmware/Console.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <chrono>

unsigned int hse_value = validation::board::hseValue;

// The BLE counterpart of validation/firmware/Main.cpp: the same console and system commands, so `ping`, `info` and
// `reset` work as everywhere, but only the `ble` group. One image serves both roles of a BLE pair test (`ble.open`).
int main()
{
    static validation::BoardInfoStm boardInfo{ validation::ReadAndClearResetCause() };
    HAL_Init();
    validation::board::InitializeClocks();

    static main_::StmEventInfrastructure eventInfrastructure;
    static validation::Console console;
    static hal::GpioPinStm debugLedPin{ validation::PortOf(validation::board::debugLed), validation::board::debugLed.index };
    static services::DebugLed debugLed{ debugLedPin, std::chrono::milliseconds(100), std::chrono::milliseconds(1400) };
    static validation::PinFactoryStm pinFactory;
    static services::HilPinPool::WithCapacity<validation::PinFactoryStm::capacity> pins{ pinFactory, infra::MakeRange(validation::board::reservedPins) };
    static services::HilPinNamingDefault naming{ validation::board::portLetters, validation::board::maximumPinIndex, infra::MakeRange(validation::board::aliases) };
    static services::HilContext context{ console.response, pins, naming, console.terminal };

    static hal::cortex::Reset reset;
    static services::HilSystemCommands system{ context, boardInfo, reset };

    static validation::BleCommands ble{ context };

    system.PrintBoot();
    eventInfrastructure.Run();
    __builtin_unreachable();
}
