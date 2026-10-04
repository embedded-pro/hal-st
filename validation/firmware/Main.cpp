#include "BoardProfile.hpp"
#include "hal/cortex_m/Reset.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinNaming.hpp"
#include "services/hil/HilPinPool.hpp"
#include "services/hil/HilSystemCommands.hpp"
#include "services/hil/commands/HilAdcCommands.hpp"
#include "services/hil/commands/HilEepromCommands.hpp"
#include "services/hil/commands/HilGpioCommands.hpp"
#include "services/hil/commands/HilPwmCommands.hpp"
#include "services/hil/commands/HilQeiCommands.hpp"
#include "services/hil/commands/HilSpiCommands.hpp"
#include "services/hil/commands/HilUartCommands.hpp"
#include "services/hil/commands/HilWatchDogCommands.hpp"
#include "services/peripheral/DebugLed.hpp"
#include "validation/firmware/AdcFactory.hpp"
#include "validation/firmware/AnalogInputGroup.hpp"
#include "validation/firmware/BoardInfoStm.hpp"
#include "validation/firmware/Console.hpp"
#include "validation/firmware/DmaGroup.hpp"
#include "validation/firmware/EepromGroup.hpp"
#include "validation/firmware/I2cGroup.hpp"
#include "validation/firmware/I2cTarget.hpp"
#include "validation/firmware/LpTimerGroup.hpp"
#include "validation/firmware/LpTimerPwmGroup.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include "validation/firmware/PwmFactory.hpp"
#include "validation/firmware/QeiFactory.hpp"
#include "validation/firmware/ResourceAllocation.hpp"
#include "validation/firmware/SpiFactory.hpp"
#include "validation/firmware/SpiSlaveGroup.hpp"
#include "validation/firmware/TimerAllocation.hpp"
#include "validation/firmware/TimerGroup.hpp"
#include "validation/firmware/TimerPwmGroup.hpp"
#include "validation/firmware/UartFactory.hpp"
#include "validation/firmware/UnsupportedGroups.hpp"
#include "validation/firmware/WatchDogFactory.hpp"
#include <chrono>

unsigned int hse_value = 32'000'000;

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
    static validation::TimerAllocation timers;
    static validation::ResourceAllocation resources;

    static hal::cortex::Reset reset;
    static services::HilSystemCommands system{ context, boardInfo, reset };
    static services::HilGpioCommands::WithMaxPins<8> gpio{ context };

    static validation::PwmFactoryStm pwmFactory{ naming, timers };
    static services::HilPwmCommands pwm{ context, pwmFactory };

    static validation::UartFactoryStm uartFactory{ naming, console.dma };
    static services::HilUartCommands::WithCapacity<256, 112> uart{ context, uartFactory };

    static validation::SpiFactoryStm spiFactory{ naming, console.dma, resources };
    static services::HilSpiCommands::WithCapacity<64> spi{ context, spiFactory };

    static validation::AdcFactoryStm adcFactory{ naming, console.dma, timers, resources };
    static services::HilAdcCommands::WithCapacity<validation::AdcFactoryStm::slots, 64> adc{ context, adcFactory };

    static validation::QeiFactoryStm qeiFactory{ naming, timers, resources };
    static services::HilQeiCommands qei{ context, qeiFactory };
    static validation::QeiExtensionCommands qeiExtension{ context, qeiFactory };

    static validation::WatchDogFactoryStm watchDogFactory{ naming, pins };
    static services::HilWatchDogCommands watchDog{ context, watchDogFactory };

    static validation::I2cFactoryStm i2cFactory{ naming, resources };
    static validation::I2cCommands i2c{ context, i2cFactory };
    static validation::I2cTargetFactory i2cTargetFactory{ naming, resources };
    static validation::I2cTargetCommands i2cTarget{ context, i2cTargetFactory };
    static validation::EepromGroup eepromGroup{ context, naming, resources };
    static services::HilEepromCommands::WithCapacity<128> eeprom{ context, eepromGroup };

    static validation::SpiSlaveFactory spiSlaveFactory{ naming, console.dma, resources };
    static validation::SpiSlaveCommands spiSlave{ context, spiSlaveFactory };

    static validation::TimerFactoryStm timerFactory{ naming, timers };
    static validation::TimerGroup timer{ context, timerFactory };
    static validation::TimerPwmFactoryStm timerPwmFactory{ naming, timers };
    static validation::TimerPwmGroup timerPwm{ context, timerPwmFactory };
#if defined(HAS_PERIPHERAL_LPTIMER)
    static validation::LpTimerFactoryStm lpTimerFactory{ naming, resources };
    static validation::LpTimerGroup lpTimer{ context, lpTimerFactory };
#endif
#if defined(HAS_PERIPHERAL_LPTIMER) && !defined(STM32WB)
    static validation::LpTimerPwmFactoryStm lpTimerPwmFactory{ naming, resources };
    static validation::LpTimerPwmGroup lpTimerPwm{ context, lpTimerPwmFactory };
#endif

    static validation::AnalogInputCommands analogInput{ context, naming, console.dma, timers, resources };
    static validation::DmaCommands dmaWave{ context, naming, console.dma, timers, resources };

    validation::CreateUnsupportedGroups(context);

    system.PrintBoot();
    eventInfrastructure.Run();
    __builtin_unreachable();
}
