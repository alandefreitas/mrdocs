#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
# Copyright (c) 2026 Gennaro Prota (gennaro.prota@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

cmake_minimum_required(VERSION 3.20)
project(mrdocs_cmake_consumer LANGUAGES CXX C)

# Use mrdocs as a library and run it as a test. Discovery is driven by the
# exact-case mrdocs_ROOT (honored since CMake 3.12), so no policy opt-in is needed.
find_package(mrdocs REQUIRED)
add_executable(consumer src/main.cpp)
target_link_libraries(consumer PRIVATE mrdocs::mrdocs-core)
target_compile_features(consumer PRIVATE cxx_std_23)
enable_testing()
add_test(NAME consumer-runs COMMAND consumer)

# A plain library, present so there is some project code to document.
add_library(example_calc src/calculator.cpp)
target_include_directories(example_calc PUBLIC include)
target_compile_features(example_calc PUBLIC cxx_std_20)
add_custom_target(consumer-docs ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/reference
        COMMAND ${MRDOCS_EXECUTABLE}
            --config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml
            --compilation-database=${CMAKE_CURRENT_SOURCE_DIR}/docs/compile_commands.json
            --output=${CMAKE_CURRENT_BINARY_DIR}/reference
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/example/calculator.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml
                ${CMAKE_CURRENT_SOURCE_DIR}/docs/compile_commands.json
        COMMENT "Generate consumer docs with MrDocs"
        USES_TERMINAL)

set(MRDOCS_CONSUMER_FOREIGN_CXX "" CACHE STRING
    "A C++ compiler of another family than CMAKE_CXX_COMPILER, as a path or a name found on PATH, to build a second copy of the plugin (Linux only)")
if (NOT MRDOCS_BUILT_PLUGIN_MODULES AND MRDOCS_CONSUMER_FOREIGN_CXX)
    message(STATUS
        "The installed MrDocs cannot load plugins (MRDOCS_BUILT_PLUGIN_MODULES is off): "
        "the plugin tests, including the build with ${MRDOCS_CONSUMER_FOREIGN_CXX}, are skipped")
endif ()

if (MRDOCS_BUILT_PLUGIN_MODULES)
    add_library(consumer_plugin MODULE src/plugin.cpp)
    target_link_libraries(consumer_plugin PRIVATE mrdocs::plugin)
    target_compile_features(consumer_plugin PRIVATE cxx_std_17)

    set(plugin_addons "${CMAKE_CURRENT_BINARY_DIR}/plugin-addons")
    set_target_properties(consumer_plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "$<1:${plugin_addons}/plugins>")

    add_test(NAME consumer-plugin-loads
        COMMAND ${MRDOCS_EXECUTABLE}
            --config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml
            --compilation-database=${CMAKE_CURRENT_SOURCE_DIR}/docs/compile_commands.json
            --output=${CMAKE_CURRENT_BINARY_DIR}/plugin-reference
            --addons-supplemental=${plugin_addons}
            --generator=consumer-probe)

    # A plugin shares no C++ with the MrDocs that loads it, so it can be built
    # by a compiler of another family than the one that built MrDocs. Naming a
    # C++ compiler in MRDOCS_CONSUMER_FOREIGN_CXX compiles the same plugin
    # with it, outside the project's own toolchain, and loads the result:
    # CI builds it with clang against a gcc-built MrDocs and the other way
    # round. See README.md. The command mirrors what a plugin author types
    # on Linux, where an ELF module leaves the symbols of MrDocs undefined
    # and the tool supplies them when it loads the library.
    if (MRDOCS_CONSUMER_FOREIGN_CXX)
        if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
            message(FATAL_ERROR
                "MRDOCS_CONSUMER_FOREIGN_CXX is for Linux, where the plugin links nothing")
        endif ()
        unset(foreign_cxx CACHE)
        find_program(foreign_cxx NAMES "${MRDOCS_CONSUMER_FOREIGN_CXX}")
        if (NOT foreign_cxx)
            message(FATAL_ERROR
                "MRDOCS_CONSUMER_FOREIGN_CXX \"${MRDOCS_CONSUMER_FOREIGN_CXX}\" is not a program")
        endif ()
        execute_process(
            COMMAND "${foreign_cxx}" --version
            OUTPUT_VARIABLE foreign_version
            RESULT_VARIABLE foreign_result
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (NOT foreign_result EQUAL 0)
            message(FATAL_ERROR
                "MRDOCS_CONSUMER_FOREIGN_CXX \"${MRDOCS_CONSUMER_FOREIGN_CXX}\" does not run")
        endif ()
        if (foreign_version MATCHES "clang")
            set(foreign_id Clang)
        else ()
            set(foreign_id GNU)
        endif ()
        if (foreign_id STREQUAL CMAKE_CXX_COMPILER_ID)
            message(FATAL_ERROR
                "MRDOCS_CONSUMER_FOREIGN_CXX is a ${foreign_id} compiler, the family of "
                "CMAKE_CXX_COMPILER; it has to be the other one")
        endif ()
        string(REGEX REPLACE "\n.*" "" foreign_version "${foreign_version}")
        message(STATUS "Consumer plugin also built with: ${foreign_version}")

        get_target_property(plugin_includes mrdocs::plugin
            INTERFACE_INCLUDE_DIRECTORIES)
        set(foreign_include_flags)
        foreach (dir IN LISTS plugin_includes)
            list(APPEND foreign_include_flags "-I${dir}")
        endforeach ()

        set(foreign_addons "${CMAKE_CURRENT_BINARY_DIR}/foreign-plugin-addons")
        set(foreign_library "${foreign_addons}/plugins/consumer_plugin_foreign.so")
        add_custom_command(
            OUTPUT "${foreign_library}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${foreign_addons}/plugins"
            COMMAND "${foreign_cxx}"
                -std=c++17 -fPIC -shared -fvisibility=hidden
                ${foreign_include_flags}
                -MD -MT "${foreign_library}" -MF "${foreign_library}.d"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
                -o "${foreign_library}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
            DEPFILE "${foreign_library}.d"
            COMMENT "Building the consumer plugin with ${foreign_cxx}"
            VERBATIM)
        add_custom_target(consumer_plugin_foreign ALL
            DEPENDS "${foreign_library}")

        add_test(NAME consumer-plugin-foreign-compiler-loads
            COMMAND ${MRDOCS_EXECUTABLE}
                --config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml
                --compilation-database=${CMAKE_CURRENT_SOURCE_DIR}/docs/compile_commands.json
                --output=${CMAKE_CURRENT_BINARY_DIR}/foreign-plugin-reference
                --addons-supplemental=${foreign_addons}
                --generator=consumer-probe)
    endif ()
endif ()
