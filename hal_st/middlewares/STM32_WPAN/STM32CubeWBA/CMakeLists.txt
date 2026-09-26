# cmake-dependency-scan pkg:github/STMicroelectronics/stm32-mw-wpan@7e764835982f0a8b94fdd6b2b056d6e0afc4d1ba

add_library(hal_st.stm32_wpan_stm32wbaxx STATIC)
emil_build_for(hal_st.stm32_wpan_stm32wbaxx TARGET_MCU_FAMILY stm32wbaxx PREREQUISITE_BOOL HALST_STANDALONE)

set_target_properties(hal_st.stm32_wpan_stm32wbaxx PROPERTIES COMPILE_DEFINITIONS "BLE_LL")

# Selects the basic feature set from the single host stack library, matching the basic link layer library
target_compile_definitions(hal_st.stm32_wpan_stm32wbaxx PUBLIC
    BLE_STACK_BASIC_FEATURES=1
)

target_include_directories(hal_st.stm32_wpan_stm32wbaxx PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/app_specific>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/ble>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/ble/stack/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/ble/stack/include/auto>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/ble/svc/Inc>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/link_layer/ll_cmd_lib/config/ble_basic>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/link_layer/ll_cmd_lib/inc>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/link_layer/ll_cmd_lib/inc/ot_inc>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/link_layer/ll_sys/inc>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(hal_st.stm32_wpan_stm32wbaxx PUBLIC
    st.hal_driver
)

target_sources(hal_st.stm32_wpan_stm32wbaxx PRIVATE
    ble/svc/Inc/svc_ctl.h
    ble/svc/Src/svc_ctl.c

    # Link layer
    link_layer/ll_cmd_lib/config/ble_basic/ll_fw_config.h
    link_layer/ll_cmd_lib/inc/_40nm_reg_files/DWC_ble154combo.h
    link_layer/ll_cmd_lib/inc/ot_inc/crypto.h
    link_layer/ll_cmd_lib/inc/ot_inc/error.h
    link_layer/ll_cmd_lib/inc/ot_inc/instance.h
    link_layer/ll_cmd_lib/inc/ot_inc/logging.h
    link_layer/ll_cmd_lib/inc/ot_inc/radio.h
    link_layer/ll_cmd_lib/inc/ot_inc/toolchain.h
    link_layer/ll_cmd_lib/inc/ot_inc/types.h
    link_layer/ll_cmd_lib/inc/bsp.h
    link_layer/ll_cmd_lib/inc/common_types.h
    link_layer/ll_cmd_lib/inc/dtm.h
    link_layer/ll_cmd_lib/inc/event_manager.h
    link_layer/ll_cmd_lib/inc/evnt_schdlr_gnrc_if.h
    link_layer/ll_cmd_lib/inc/hci.h
    link_layer/ll_cmd_lib/inc/ll_error.h
    link_layer/ll_cmd_lib/inc/ll_intf.h
    link_layer/ll_cmd_lib/inc/ll_intf_cmn.h
    link_layer/ll_cmd_lib/inc/mac_host_intf.h
    link_layer/ll_cmd_lib/inc/mem_intf.h
    link_layer/ll_cmd_lib/inc/os_wrapper.h
    link_layer/ll_cmd_lib/inc/platform.h
    link_layer/ll_cmd_lib/inc/power_table.h
    link_layer/ll_cmd_lib/inc/pta.h
    link_layer/ll_cmd_lib/inc/ral.h
    link_layer/ll_cmd_lib/inc/rfd_dev_config.h

    link_layer/ll_sys/inc/linklayer_plat.h
    link_layer/ll_sys/inc/ll_sys_sequencer.h
    link_layer/ll_sys/inc/ll_sys_startup.h
    link_layer/ll_sys/inc/ll_sys.h

    link_layer/ll_sys/src/ll_sys_cs.c
    link_layer/ll_sys/src/ll_sys_dp_slp.c
    link_layer/ll_sys/src/ll_sys_intf.c
    link_layer/ll_sys/src/ll_sys_startup.c
)

add_library(hal_st.stm32_wpan_stm32wbaxx.link_layer_basic STATIC IMPORTED GLOBAL)
set_target_properties(hal_st.stm32_wpan_stm32wbaxx.link_layer_basic PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_LIST_DIR}/link_layer/ll_cmd_lib/lib/LinkLayer_BLE_Basic_V3_0.a)

add_library(hal_st.stm32_wpan_stm32wbaxx.stack STATIC IMPORTED GLOBAL)
set_target_properties(hal_st.stm32_wpan_stm32wbaxx.stack PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_LIST_DIR}/ble/stack/lib/stm32wba_ble_stack_full.a)
