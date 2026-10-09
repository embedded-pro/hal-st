add_executable(demo_st.ble_central Main.cpp)
emil_build_for(demo_st.ble_central
    TARGET_MCU stm32wb55
    TARGET_MCU stm32wba55
    PREREQUISITE_BOOL HALST_BUILD_DEMOS
)

target_compile_definitions(demo_st.ble_central PUBLIC
    $<$<STREQUAL:${TARGET_MCU},stm32wb55>:NUCLEO=Nucleo64WBUi>
    $<$<STREQUAL:${TARGET_MCU},stm32wba55>:NUCLEO=Nucleo64WBA55Ui>
)

target_link_libraries(demo_st.ble_central PRIVATE
    services.util
    services.ble
    services.ble_profile
    hal_st.instantiations
    hal_st.ble_middleware
)

halst_target_default_linker_scripts(demo_st.ble_central)
halst_target_bringup(demo_st.ble_central)

emil_generate_artifacts(TARGET demo_st.ble_central LST MAP BIN HEX)
