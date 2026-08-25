add_library(hal_st.bringup STATIC)
emil_build_for(hal_st.bringup TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_STANDALONE)

target_include_directories(hal_st.bringup PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(hal_st.bringup PUBLIC
    st.hal_driver
    hal.cortex_m
)

# Assembler does not understand -Werror
set_target_properties(hal_st.bringup PROPERTIES COMPILE_WARNING_AS_ERROR Off)

target_sources(hal_st.bringup PRIVATE
    Assert.cpp
    Bringup.cpp
)

if (TARGET_MCU_VENDOR STREQUAL st)
    get_target_property(startup_source st.hal_driver_${TARGET_MCU_FAMILY} HALST_STARTUP_SOURCE)

    target_sources(hal_st.bringup PRIVATE
        ${startup_source}
    )
endif()

function(halst_target_bringup target)
    # hal.cortex_m's FaultTracer defines HardFault_Handler etc. as weak symbols, same as the
    # vendor startup file's ".thumb_set HardFault_Handler,Default_Handler" alias; whichever
    # definition the linker sees first for a given weak symbol wins, so hal.cortex_m's objects
    # are listed ahead of hal_st.bringup's (which carries the startup object) to make
    # FaultTracer's fault handlers win deterministically instead of by accident.
    #
    # hal.cortex_m.runtime (Atomic/DefaultInit/SysCallStubs) supplies abort(), _sbrk, and the
    # __atomic_* shims as weak symbols; per its own design, these must reach the final ELF as
    # loose objects rather than archive members, because an archived weak definition is only
    # extracted once something already references it, which is too late to beat what newlib
    # pulls out of libg_nano.a.
    target_link_libraries(${target} PUBLIC
        $<TARGET_OBJECTS:hal.cortex_m>
        $<TARGET_OBJECTS:hal_st.bringup>
        $<TARGET_OBJECTS:hal.cortex_m.runtime>
    )
endfunction()
