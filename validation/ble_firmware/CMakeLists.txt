add_executable(hal_st.validation_ble_firmware)
emil_build_for(hal_st.validation_ble_firmware
    TARGET_MCU stm32wb55
    TARGET_MCU stm32wba55
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
)

target_include_directories(hal_st.validation_ble_firmware PRIVATE
    ../firmware/boards/${TARGET_MCU}
)

target_link_libraries(hal_st.validation_ble_firmware PRIVATE
    services.hil
    services.ble
    services.ble_profile
    services.util
    services.peripheral
    services.tracer
    hal_st.ble_middleware
    hal_st.instantiations
    hal_st.stm32fxxx
    hal_st.synchronous_stm32fxxx
)

target_sources(hal_st.validation_ble_firmware PRIVATE
    BleController.cpp
    BleController.hpp
    BleGroup.cpp
    BleGroup.hpp
    BleReporter.cpp
    BleReporter.hpp
    BleRoles.cpp
    BleRoles.hpp
    Main.cpp
    ../firmware/BoardInfoStm.cpp
    ../firmware/BoardInfoStm.hpp
    ../firmware/BoardTypes.hpp
    ../firmware/Console.cpp
    ../firmware/Console.hpp
    ../firmware/Payload.cpp
    ../firmware/Payload.hpp
    ../firmware/PinFactoryStm.cpp
    ../firmware/PinFactoryStm.hpp
    $<$<STREQUAL:${TARGET_MCU},stm32wb55>:../firmware/boards/stm32wb55/BoardProfile.hpp>
    $<$<STREQUAL:${TARGET_MCU},stm32wba55>:../firmware/boards/stm32wba55/BoardProfile.hpp>
)

halst_target_default_linker_scripts(hal_st.validation_ble_firmware)
halst_target_bringup(hal_st.validation_ble_firmware)

emil_generate_artifacts(TARGET hal_st.validation_ble_firmware LST MAP BIN HEX)
