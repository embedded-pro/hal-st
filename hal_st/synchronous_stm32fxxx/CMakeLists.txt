add_library(hal_st.synchronous_stm32fxxx STATIC)
emil_build_for(hal_st.synchronous_stm32fxxx TARGET_MCU_VENDOR st PREREQUISITE_BOOL HALST_STANDALONE)

target_include_directories(hal_st.synchronous_stm32fxxx PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(hal_st.synchronous_stm32fxxx PUBLIC
    hal.synchronous_interfaces
    hal_st.stm32fxxx
    services.crypto
)

target_sources(hal_st.synchronous_stm32fxxx PRIVATE
    SynchronousAesStm.cpp
    SynchronousAesStm.hpp
    SynchronousGpioStm.cpp
    SynchronousGpioStm.hpp
    $<$<NOT:$<OR:$<STREQUAL:${TARGET_MCU_FAMILY},stm32h5xx>,$<STREQUAL:${TARGET_MCU_FAMILY},stm32h7xx>>>:SynchronousFlashInternalStm.cpp>
    $<$<NOT:$<OR:$<STREQUAL:${TARGET_MCU_FAMILY},stm32h5xx>,$<STREQUAL:${TARGET_MCU_FAMILY},stm32h7xx>>>:SynchronousFlashInternalStm.hpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:SynchronousHardwareSemaphoreStm.cpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:SynchronousHardwareSemaphoreStm.hpp>
    SynchronousPwmStm.cpp
    SynchronousPwmStm.hpp
    SynchronousQuadratureEncoderLpTimStm.cpp
    SynchronousQuadratureEncoderLpTimStm.hpp
    SynchronousQuadratureEncoderStm.cpp
    SynchronousQuadratureEncoderStm.hpp
    SynchronousRandomDataGeneratorStm.cpp
    SynchronousRandomDataGeneratorStm.hpp
    $<$<NOT:$<STREQUAL:${TARGET_MCU_FAMILY},stm32h7xx>>:SynchronousSpiMasterStm.cpp>
    $<$<NOT:$<STREQUAL:${TARGET_MCU_FAMILY},stm32h7xx>>:SynchronousSpiMasterStm.hpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:SynchronousSynchronizedRandomDataGeneratorStm.cpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32wbxx>:SynchronousSynchronizedRandomDataGeneratorStm.hpp>
    $<$<NOT:$<STREQUAL:${TARGET_MCU_FAMILY},stm32g0xx>>:SynchronousUartStm.cpp>
    $<$<NOT:$<STREQUAL:${TARGET_MCU_FAMILY},stm32g0xx>>:SynchronousUartStm.hpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32f7xx>:SynchronousQuadSpiStm.cpp>
    $<$<STREQUAL:${TARGET_MCU_FAMILY},stm32f7xx>:SynchronousQuadSpiStm.hpp>
)
