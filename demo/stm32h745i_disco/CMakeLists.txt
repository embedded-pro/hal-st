set(STM32H745I_DISCO_CM7 Off)
set(STM32H745I_DISCO_CM4 Off)
if (TARGET_MCU STREQUAL stm32h745)
    if (TARGET_CORTEX STREQUAL m7)
        set(STM32H745I_DISCO_CM7 On)
    elseif (TARGET_CORTEX STREQUAL m4)
        set(STM32H745I_DISCO_CM4 On)
    endif()
endif()

add_executable(demo_st.stm32h745i_disco_cm7
    DefaultClockDiscoveryH745I.cpp
    DefaultClockDiscoveryH745I.hpp
    DiscoveryH745Ui.hpp
    DiscoveryMemories.hpp
    MainCm7.cpp
)
emil_build_for(demo_st.stm32h745i_disco_cm7
    TARGET_MCU stm32h745
    PREREQUISITE_BOOL HALST_BUILD_DEMOS STM32H745I_DISCO_CM7
)

target_link_libraries(demo_st.stm32h745i_disco_cm7 PRIVATE
    demo_st.common
    services.flash
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(demo_st.stm32h745i_disco_cm7)
halst_target_bringup(demo_st.stm32h745i_disco_cm7)

emil_generate_artifacts(TARGET demo_st.stm32h745i_disco_cm7 LST MAP BIN HEX)

######################################################################

add_executable(demo_st.stm32h745i_disco_cm4
    DiscoveryH745Ui.hpp
    MainCm4.cpp
)
emil_build_for(demo_st.stm32h745i_disco_cm4
    TARGET_MCU stm32h745
    PREREQUISITE_BOOL HALST_BUILD_DEMOS STM32H745I_DISCO_CM4
)

target_link_libraries(demo_st.stm32h745i_disco_cm4 PRIVATE
    demo_st.common
    services.util
    services.peripheral
    hal_st.instantiations
)

halst_target_default_linker_scripts(demo_st.stm32h745i_disco_cm4)
halst_target_bringup(demo_st.stm32h745i_disco_cm4)

emil_generate_artifacts(TARGET demo_st.stm32h745i_disco_cm4 LST MAP BIN HEX)
