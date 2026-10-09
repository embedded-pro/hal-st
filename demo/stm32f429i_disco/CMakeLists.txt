add_executable(demo_st.stm32f429i_disco
    DefaultClockDisco429I.cpp
    DefaultClockDisco429I.hpp
    Main.cpp
)
emil_build_for(demo_st.stm32f429i_disco
    TARGET_MCU stm32f429
    PREREQUISITE_BOOL HALST_BUILD_DEMOS
)

target_link_libraries(demo_st.stm32f429i_disco PRIVATE
    boards.stm32f429i_disco_lcd
    drivers.imu.l3gd20
    drivers.touch_screen.stmpe811
    demo_st.display_demo
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(demo_st.stm32f429i_disco)
halst_target_bringup(demo_st.stm32f429i_disco)

emil_generate_artifacts(TARGET demo_st.stm32f429i_disco LST MAP BIN HEX)
