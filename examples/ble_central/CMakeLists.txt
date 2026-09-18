set(ble_example_mcus TARGET_MCU stm32wb55)

if (HALST_BUILD_EXAMPLES_BLE_WBA)
    list(APPEND ble_example_mcus TARGET_MCU stm32wba55)
endif()

add_executable(examples_st.ble_central Main.cpp)
emil_build_for(examples_st.ble_central
    ${ble_example_mcus}
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
)

target_compile_definitions(examples_st.ble_central PUBLIC
    $<$<STREQUAL:${TARGET_MCU},stm32wb55>:NUCLEO=Nucleo64WBUi>
    $<$<STREQUAL:${TARGET_MCU},stm32wba55>:NUCLEO=Nucleo64WBA55Ui>
)

target_link_libraries(examples_st.ble_central PRIVATE
    services.util
    services.ble
    services.ble_profile
    hal_st.instantiations
    hal_st.ble_middleware
)

halst_target_default_linker_scripts(examples_st.ble_central)
halst_target_bringup(examples_st.ble_central)

emil_generate_artifacts(TARGET examples_st.ble_central LST MAP BIN HEX)
