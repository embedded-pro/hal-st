add_executable(examples_st.stm32f429i_disco
    DefaultClockDisco429I.cpp
    DefaultClockDisco429I.hpp
    Main.cpp
)
emil_build_for(examples_st.stm32f429i_disco
    TARGET_MCU stm32f429
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
)

target_link_libraries(examples_st.stm32f429i_disco PRIVATE
    boards.stm32f429i_disco_lcd
    examples_st.display_demo
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(examples_st.stm32f429i_disco)
halst_target_bringup(examples_st.stm32f429i_disco)

emil_generate_artifacts(TARGET examples_st.stm32f429i_disco LST MAP BIN HEX)
