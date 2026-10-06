set(STM32H757I_EVAL_CM7 Off)
set(STM32H757I_EVAL_CM4 Off)
if (TARGET_MCU STREQUAL stm32h757)
    if (TARGET_CORTEX STREQUAL m7)
        set(STM32H757I_EVAL_CM7 On)
    elseif (TARGET_CORTEX STREQUAL m4)
        set(STM32H757I_EVAL_CM4 On)
    endif()
endif()

add_executable(examples_st.stm32h757i_eval_cm7
    DefaultClockEvalH757I.cpp
    DefaultClockEvalH757I.hpp
    EvalTracerInfrastructure.hpp
    EvalUi.hpp
    InvertedGpioPin.hpp
    MainCm7.cpp
)
emil_build_for(examples_st.stm32h757i_eval_cm7
    TARGET_MCU stm32h757
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES STM32H757I_EVAL_CM7
)

target_link_libraries(examples_st.stm32h757i_eval_cm7 PRIVATE
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(examples_st.stm32h757i_eval_cm7)
halst_target_bringup(examples_st.stm32h757i_eval_cm7)

emil_generate_artifacts(TARGET examples_st.stm32h757i_eval_cm7 LST MAP BIN HEX)

######################################################################

add_executable(examples_st.stm32h757i_eval_cm4
    EvalUi.hpp
    InvertedGpioPin.hpp
    MainCm4.cpp
)
emil_build_for(examples_st.stm32h757i_eval_cm4
    TARGET_MCU stm32h757
    PREREQUISITE_BOOL HALST_BUILD_EXAMPLES STM32H757I_EVAL_CM4
)

target_link_libraries(examples_st.stm32h757i_eval_cm4 PRIVATE
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(examples_st.stm32h757i_eval_cm4)
halst_target_bringup(examples_st.stm32h757i_eval_cm4)

emil_generate_artifacts(TARGET examples_st.stm32h757i_eval_cm4 LST MAP BIN HEX)
