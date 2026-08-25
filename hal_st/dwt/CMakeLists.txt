add_library(hal_st.dwt STATIC)
emil_build_for(hal_st.dwt TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_STANDALONE)

target_include_directories(hal_st.dwt PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(hal_st.dwt PUBLIC
    st.hal_driver
    infra.util
)

target_sources(hal_st.dwt PRIVATE
    DataWatchpointAndTrace.hpp
    DataWatchpointAndTrace.cpp
)
