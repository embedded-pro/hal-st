add_library(hal_st.ble_middleware STATIC)
emil_build_for(hal_st.ble_middleware
    TARGET_MCU_FAMILY stm32wbxx
    TARGET_MCU_FAMILY stm32wbaxx
    PREREQUISITE_BOOL HALST_STANDALONE)

target_include_directories(hal_st.ble_middleware PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(hal_st.ble_middleware PUBLIC
    hal_st.stm32_wpan
    services.ble
    services.util
    services.tracer
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbaxx>:$<LINK_GROUP:RESCAN,hal_st.ble_middleware_wba_platform,hal_st.stm32_wpan_stm32wbaxx,hal_st.stm32_wpan_stm32wbaxx.link_layer_basic,hal_st.stm32_wpan_stm32wbaxx.stack_basic>>
)

target_sources(hal_st.ble_middleware PRIVATE
    BondStorageSt.cpp
    BondStorageSt.hpp
    DirectTestModeSt.cpp
    DirectTestModeSt.hpp
    GapCentralSt.cpp
    GapCentralSt.hpp
    GapPeripheralSt.cpp
    GapPeripheralSt.hpp
    GapSt.cpp
    GapSt.hpp
    GattClientSt.cpp
    GattClientSt.hpp
    GattServerSt.cpp
    GattServerSt.hpp
    HciEventObserver.hpp
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:SystemTransportLayerWb.cpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:SystemTransportLayerWb.hpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbaxx>:SystemTransportLayerWba.cpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbaxx>:SystemTransportLayerWba.hpp>
    TracingGapCentralSt.cpp
    TracingGapCentralSt.hpp
    TracingGapPeripheralSt.cpp
    TracingGapPeripheralSt.hpp
    TracingGattClientSt.cpp
    TracingGattClientSt.hpp
    TracingGattServerSt.cpp
    TracingGattServerSt.hpp
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:TracingSystemTransportLayerWb.cpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:TracingSystemTransportLayerWb.hpp>
)

add_library(hal_st.ble_middleware_wba_platform STATIC)
emil_build_for(hal_st.ble_middleware_wba_platform
    TARGET_MCU_FAMILY stm32wbaxx
    PREREQUISITE_BOOL HALST_STANDALONE)

target_link_libraries(hal_st.ble_middleware_wba_platform PUBLIC
    hal_st.stm32_wpan_stm32wbaxx
    infra.util
)

target_sources(hal_st.ble_middleware_wba_platform PRIVATE
    BlePlatformWba.cpp
    LinkLayerPlatformWba.cpp
    PowerTableWba.cpp
)
