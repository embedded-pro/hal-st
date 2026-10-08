if (EMIL_INCLUDE_SEGGER_RTT)
    add_executable(examples_st.stm32f407g_disco
        Main.cpp
        PdmDecimator.cpp
        PdmDecimator.hpp
    )
    emil_build_for(examples_st.stm32f407g_disco
        TARGET_MCU stm32f407
        PREREQUISITE_BOOL HALST_BUILD_EXAMPLES
    )

    target_link_libraries(examples_st.stm32f407g_disco PRIVATE
        drivers.audio.cs43l22
        drivers.imu.lis3dsh
        drivers.microphones.mp45dt02
        examples_st.audio_demo
        services.peripheral
        services.util
        hal_st.instantiations
    )

    halst_target_default_linker_scripts(examples_st.stm32f407g_disco)
    halst_target_bringup(examples_st.stm32f407g_disco)

    emil_generate_artifacts(TARGET examples_st.stm32f407g_disco LST MAP BIN HEX)
elseif (TARGET_MCU STREQUAL stm32f407)
    message(STATUS "examples_st.stm32f407g_disco traces over SEGGER RTT and is skipped without EMIL_INCLUDE_SEGGER_RTT")
endif()
