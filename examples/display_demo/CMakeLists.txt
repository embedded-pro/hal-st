add_library(examples_st.display_demo STATIC)
emil_build_for(examples_st.display_demo TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_BUILD_EXAMPLES)

target_include_directories(examples_st.display_demo PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(examples_st.display_demo PUBLIC
    hal.interfaces
    infra.util
)

target_sources(examples_st.display_demo PRIVATE
    DisplayDemo.cpp
    DisplayDemo.hpp
)
