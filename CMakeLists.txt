cmake_minimum_required(VERSION 3.24)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(HALST_STANDALONE On)
endif()

option(CMAKE_COMPILE_WARNING_AS_ERROR "Enable warnings-as-error" ON)
option(HALST_BUILD_TESTS "Enable building the tests" Off)
option(HALST_INCLUDE_DEFAULT_INIT "Include default initialization code; turn off when providing custom initialization" ON)
option(HALST_BUILD_EXAMPLES "Enable build of the examples" OFF)
option(HALST_BUILD_EXAMPLES_FREERTOS "Enable build of the FreeRTOS example" OFF)
set(HALST_XML_GPIO "" CACHE STRING "XML GPIO table")
set(HALST_XML_STM32 "" CACHE STRING "XML STM32 table")

if (HALST_BUILD_TESTS)
    # CTest cannot be included before the first project() statement, but amp-embedded-infa-lib
    # needs to see that test utilities need to be built.
    set(BUILD_TESTING On)
endif()

if (HALST_STANDALONE)
    set(EMIL_INCLUDE_FREERTOS ${HALST_BUILD_EXAMPLES_FREERTOS})

    include(FetchContent)

    FetchContent_Declare(
        emil
        GIT_REPOSITORY https://github.com/embedded-pro/embedded-infra-lib.git
        GIT_TAG        bce09c3931ec821d956be5c62a71db5c264718fb  # Unreleased
    )

    add_definitions(-DEMIL_ENABLE_TRACING=1)

    set(EMIL_ENABLE_DOCKER_TOOLS Off CACHE BOOL "" FORCE)
    set(EMIL_BUILD_ECHO_COMPILERS On CACHE BOOL "" FORCE)

    # hal.cortex_m.runtime contains ARM-only inline assembly; only build it when actually
    # cross-compiling for an ST target, not for the host toolchain (TARGET_MCU_VENDOR is
    # unset there).
    if (TARGET_MCU_VENDOR STREQUAL st)
        set(EMIL_BUILD_CORTEX_M On CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(emil)

    if (EMIL_HOST_BUILD)
        FetchContent_Declare(
            cucumber-cpp-runner
            GIT_REPOSITORY https://github.com/philips-software/amp-cucumber-cpp-runner.git
            GIT_TAG        752b9f6f82b1f2f35bc01a4ae19aff4fd43ad97f # v1.1.0
        )

        FetchContent_MakeAvailable(cucumber-cpp-runner)
    endif()
endif()

project(hal_st LANGUAGES C CXX ASM VERSION 4.0.0) # x-release-please-version

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)

set_directory_properties(PROPERTY USE_FOLDERS ON)

# When building HalSt by itself do not exclude any targets from all
# Libraries will only be excluded from all when HalSt is a consumed as a dependency.
if (HALST_STANDALONE)
    set(HALST_EXCLUDE_FROM_ALL "")
else()
    set(HALST_EXCLUDE_FROM_ALL "EXCLUDE_FROM_ALL")
endif()

if (HALST_BUILD_TESTS)
    include(CTest)
    emil_enable_testing()
endif()

add_subdirectory(st)
add_subdirectory(hal_st)
add_subdirectory(hal_st_lwip)
if (HALST_STANDALONE)
    add_subdirectory(integration_test)
endif()
add_subdirectory(services)
if (HALST_BUILD_EXAMPLES OR HALST_BUILD_EXAMPLES_FREERTOS)
    add_subdirectory(examples)
endif()

emil_clangformat_directories(hal_st DIRECTORIES .)

if (HALST_STANDALONE)
    emil_folderize_all_targets()
endif()

set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_SOURCE_IGNORE_FILES ".vs/;.git/;build/")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Koninklijke Philips N.V")
set(CPACK_PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(CPACK_DEBIAN_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_RPM_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

include(CPack)
