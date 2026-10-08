add_executable(examples_st.stm32f746g_disco
    Main.cpp
)
emil_build_for(examples_st.stm32f746g_disco
    TARGET_MCU_VARIANT stm32f746ng
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
)

target_link_libraries(examples_st.stm32f746g_disco PRIVATE
    drivers.audio.wm8994
    examples_st.audio_demo
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(examples_st.stm32f746g_disco)
halst_target_bringup(examples_st.stm32f746g_disco)

emil_generate_artifacts(TARGET examples_st.stm32f746g_disco LST MAP BIN HEX)
