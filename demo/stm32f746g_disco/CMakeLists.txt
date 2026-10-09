add_executable(demo_st.stm32f746g_disco
    Main.cpp
)
emil_build_for(demo_st.stm32f746g_disco
    TARGET_MCU_VARIANT stm32f746ng
    PREREQUISITE_BOOL HALST_BUILD_DEMOS
)

target_link_libraries(demo_st.stm32f746g_disco PRIVATE
    drivers.audio.wm8994
    demo_st.audio_demo
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(demo_st.stm32f746g_disco)
halst_target_bringup(demo_st.stm32f746g_disco)

emil_generate_artifacts(TARGET demo_st.stm32f746g_disco LST MAP BIN HEX)
