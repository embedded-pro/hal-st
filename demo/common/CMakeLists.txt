add_library(demo_st.common STATIC)
emil_build_for(demo_st.common TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_BUILD_DEMOS)

target_include_directories(demo_st.common PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(demo_st.common PUBLIC
    hal.interfaces
    infra.util
    services.flash
    services.tracer
)

target_sources(demo_st.common PRIVATE
    InvertedGpioPin.hpp
    MemoryTests.hpp
    QuadSpiMemory.cpp
    QuadSpiMemory.hpp
)
