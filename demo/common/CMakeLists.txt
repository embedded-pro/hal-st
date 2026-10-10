add_library(demo_st.common STATIC)
emil_build_for(demo_st.common TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_BUILD_DEMOS)

target_include_directories(demo_st.common PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(demo_st.common PUBLIC
    drivers.touch_screen.ft6x06
    hal.interfaces
    infra.util
    services.flash
    services.tracer
)

target_sources(demo_st.common PRIVATE
    Canvas.cpp
    Canvas.hpp
    CountingI2cStm.hpp
    DashboardStyle.hpp
    I2cScanner.cpp
    I2cScanner.hpp
    InvertedGpioPin.hpp
    MemoryTests.hpp
    QuadSpiMemory.cpp
    QuadSpiMemory.hpp
    Surfaces.hpp
    TouchCursor.cpp
    TouchCursor.hpp
    TouchMonitor.cpp
    TouchMonitor.hpp
)
