add_library(demo_st.sd_card_demo STATIC)
emil_build_for(demo_st.sd_card_demo TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_BUILD_DEMOS)

target_include_directories(demo_st.sd_card_demo PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(demo_st.sd_card_demo PUBLIC
    hal.interfaces
    infra.stream
    infra.util
    services.tracer
)

target_sources(demo_st.sd_card_demo PRIVATE
    SdCardDemo.cpp
    SdCardDemo.hpp
)
