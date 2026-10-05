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
    BoardInfoStm.cpp
    BoardInfoStm.hpp
    BoardTypes.hpp
    Console.cpp
    Console.hpp
    Main.cpp
    PeripheralClocks.cpp
    PeripheralClocks.hpp
    PinFactoryStm.cpp
    PinFactoryStm.hpp
    PwmFactory.cpp
    PwmFactory.hpp
    QeiFactory.cpp
    QeiFactory.hpp
    SpiFactory.cpp
    SpiFactory.hpp
    TimerAllocation.cpp
    TimerAllocation.hpp
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
