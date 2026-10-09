if (EMIL_INCLUDE_SEGGER_RTT)
    add_executable(demo_st.stm32f407g_disco
        Main.cpp
        PdmDecimator.cpp
        PdmDecimator.hpp
    )
    emil_build_for(demo_st.stm32f407g_disco
        TARGET_MCU stm32f407
        PREREQUISITE_BOOL HALST_BUILD_DEMOS
    )

    target_link_libraries(demo_st.stm32f407g_disco PRIVATE
        drivers.audio.cs43l22
        drivers.imu.lis3dsh
        drivers.microphones.mp45dt02
        demo_st.audio_demo
        services.peripheral
        services.util
        hal_st.instantiations
    )

    halst_target_default_linker_scripts(demo_st.stm32f407g_disco)
    halst_target_bringup(demo_st.stm32f407g_disco)

    emil_generate_artifacts(TARGET demo_st.stm32f407g_disco LST MAP BIN HEX)
elseif (TARGET_MCU STREQUAL stm32f407)
    message(STATUS "demo_st.stm32f407g_disco traces over SEGGER RTT and is skipped without EMIL_INCLUDE_SEGGER_RTT")
endif()
