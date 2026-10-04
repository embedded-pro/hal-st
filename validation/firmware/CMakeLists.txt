add_executable(hal_st.validation_firmware)
emil_build_for(hal_st.validation_firmware
    TARGET_MCU stm32wb55
    TARGET_MCU stm32wba55
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
)

target_include_directories(hal_st.validation_firmware PRIVATE
    boards/${TARGET_MCU}
)

target_link_libraries(hal_st.validation_firmware PRIVATE
    services.hil
    services.hil.commands
    services.peripheral
    services.synchronous_peripheral
    services.tracer
    hal_st.instantiations
    hal_st.stm32fxxx
    hal_st.synchronous_stm32fxxx
)

target_sources(hal_st.validation_firmware PRIVATE
    AdcFactory.cpp
    AdcFactory.hpp
    AnalogInputGroup.cpp
    AnalogInputGroup.hpp
    BoardInfoStm.cpp
    BoardInfoStm.hpp
    BoardTypes.hpp
    ChannelPins.hpp
    Console.cpp
    Console.hpp
    DmaGroup.cpp
    DmaGroup.hpp
    EepromGroup.cpp
    EepromGroup.hpp
    HsemMaster.cpp
    HsemMaster.hpp
    I2cEepromStm.cpp
    I2cEepromStm.hpp
    I2cGroup.cpp
    I2cGroup.hpp
    I2cTarget.cpp
    I2cTarget.hpp
    I2cTiming.cpp
    I2cTiming.hpp
    LpTimerGroup.cpp
    LpTimerGroup.hpp
    LpTimerPwmGroup.cpp
    LpTimerPwmGroup.hpp
    Main.cpp
    Owners.hpp
    Payload.cpp
    Payload.hpp
    PeripheralClocks.cpp
    PeripheralClocks.hpp
    PinFactoryStm.cpp
    PinFactoryStm.hpp
    PwmFactory.cpp
    PwmFactory.hpp
    QeiFactory.cpp
    QeiFactory.hpp
    ResourceAllocation.cpp
    ResourceAllocation.hpp
    SpiFactory.cpp
    SpiFactory.hpp
    SpiSlaveGroup.cpp
    SpiSlaveGroup.hpp
    Stopwatch.hpp
    TimerAllocation.cpp
    TimerAllocation.hpp
    TimerGroup.cpp
    TimerGroup.hpp
    TimerPwmGroup.cpp
    TimerPwmGroup.hpp
    UartFactory.cpp
    UartFactory.hpp
    UnsupportedGroups.cpp
    UnsupportedGroups.hpp
    WatchDogFactory.cpp
    WatchDogFactory.hpp
    $<$<STREQUAL:${TARGET_MCU},stm32wb55>:boards/stm32wb55/BoardProfile.hpp>
    $<$<STREQUAL:${TARGET_MCU},stm32wba55>:boards/stm32wba55/BoardProfile.hpp>
)

halst_target_default_linker_scripts(hal_st.validation_firmware)
halst_target_bringup(hal_st.validation_firmware)

emil_generate_artifacts(TARGET hal_st.validation_firmware LST MAP BIN HEX)
