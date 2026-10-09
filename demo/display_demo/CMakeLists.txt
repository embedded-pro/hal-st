add_library(demo_st.display_demo STATIC)
emil_build_for(demo_st.display_demo TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_BUILD_DEMOS)

target_include_directories(demo_st.display_demo PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(demo_st.display_demo PUBLIC
    hal.interfaces
    infra.util
)

target_sources(demo_st.display_demo PRIVATE
    DisplayDemo.cpp
    DisplayDemo.hpp
)
