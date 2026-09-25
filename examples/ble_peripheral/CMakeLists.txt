add_executable(examples_st.ble_peripheral Main.cpp)
emil_build_for(examples_st.ble_peripheral
    TARGET_MCU stm32wb55
    TARGET_MCU stm32wba55
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
)

target_compile_definitions(examples_st.ble_peripheral PUBLIC
    $<$<STREQUAL:${TARGET_MCU},stm32wb55>:NUCLEO=Nucleo64WBUi>
    $<$<STREQUAL:${TARGET_MCU},stm32wba55>:NUCLEO=Nucleo64WBA55Ui>
)

target_link_libraries(examples_st.ble_peripheral PRIVATE
    services.util
    services.ble
    services.ble_profile
    hal_st.instantiations
    hal_st.ble_middleware
)

halst_target_default_linker_scripts(examples_st.ble_peripheral)
halst_target_bringup(examples_st.ble_peripheral)

emil_generate_artifacts(TARGET examples_st.ble_peripheral LST MAP BIN HEX)
