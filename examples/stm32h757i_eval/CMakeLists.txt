add_executable(examples_st.stm32h757i_eval
    DefaultClockEvalH757I.cpp
    DefaultClockEvalH757I.hpp
    EvalTracerInfrastructure.hpp
    EvalUi.hpp
    InvertedGpioPin.hpp
    Main.cpp
)
emil_build_for(examples_st.stm32h757i_eval
    TARGET_MCU stm32h757
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
)

target_link_libraries(examples_st.stm32h757i_eval PRIVATE
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(examples_st.stm32h757i_eval)
halst_target_bringup(examples_st.stm32h757i_eval)

emil_generate_artifacts(TARGET examples_st.stm32h757i_eval LST MAP BIN HEX)
