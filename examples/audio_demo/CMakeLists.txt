add_library(examples_st.audio_demo STATIC)
emil_build_for(examples_st.audio_demo TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_BUILD_EXAMPLES)

target_include_directories(examples_st.audio_demo PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(examples_st.audio_demo PUBLIC
    hal.interfaces
    infra.util
)

target_sources(examples_st.audio_demo PRIVATE
    ToneDemo.cpp
    ToneDemo.hpp
)
