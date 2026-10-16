# Included by project(emil) through CMAKE_PROJECT_emil_INCLUDE for cross builds. EMIL only finds the
# Echo plugins of a cross build in an installed EMIL package, so a clean checkout would not configure.
# Build them for the host from the same sources instead and hand EMIL the targets it looks for.
include(ExternalProject)

set(native_binary_dir "${CMAKE_BINARY_DIR}/_native_echo_plugins")

ExternalProject_Add(emil_native_echo_plugins
    SOURCE_DIR "${PROJECT_SOURCE_DIR}"
    BINARY_DIR "${native_binary_dir}"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_TOOLCHAIN_FILE="
        "-DEMIL_BUILD_ECHO_COMPILERS=On"
        "-DEMIL_BUILD_TESTS=Off"
        "-DEMIL_BUILD_EXAMPLES=Off"
        "-DEMIL_ENABLE_DOCKER_TOOLS=Off"
        "-DEMIL_INCLUDE_LWIP=Off"
        "-DEMIL_INCLUDE_MBEDTLS=Off"
    BUILD_COMMAND
        ${CMAKE_COMMAND} --build <BINARY_DIR>
        --target
            application.protoc_echo_plugin
            application.protoc_echo_plugin_csharp
            application.protoc_echo_plugin_java
    BUILD_BYPRODUCTS
        "${native_binary_dir}/application/protoc_echo_plugin/application.protoc_echo_plugin${CMAKE_HOST_EXECUTABLE_SUFFIX}"
        "${native_binary_dir}/application/protoc_echo_plugin_csharp/application.protoc_echo_plugin_csharp${CMAKE_HOST_EXECUTABLE_SUFFIX}"
        "${native_binary_dir}/application/protoc_echo_plugin_java/application.protoc_echo_plugin_java${CMAKE_HOST_EXECUTABLE_SUFFIX}"
    INSTALL_COMMAND ""
    BUILD_ALWAYS OFF
)

foreach(plugin IN ITEMS protoc_echo_plugin protoc_echo_plugin_csharp protoc_echo_plugin_java)
    add_executable(application.${plugin} IMPORTED GLOBAL)
    set_target_properties(application.${plugin} PROPERTIES
        IMPORTED_LOCATION "${native_binary_dir}/application/${plugin}/application.${plugin}${CMAKE_HOST_EXECUTABLE_SUFFIX}"
    )
    add_dependencies(application.${plugin} emil_native_echo_plugins)
endforeach()
