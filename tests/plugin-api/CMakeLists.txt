#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Gennaro Prota (gennaro.prota@gmail.com)
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# The header a plugin includes has to be valid C11 on its own, since plugins
# can be written in C: it is compiled as C, with warnings as errors where the
# compiler has the flags, and without linking to anything.
add_library(mrdocs-plugin-api-c-header OBJECT CHeader.c)
target_include_directories(mrdocs-plugin-api-c-header PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
set_target_properties(mrdocs-plugin-api-c-header PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
target_compile_options(mrdocs-plugin-api-c-header PRIVATE
    $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic -Wmissing-prototypes -Werror>)

# A module library built and linked the way a plugin is, so that building it
# is the test: see LinkProbe.cpp for what that catches and what it does not.
# It is deliberately not registered with ctest, since it runs nothing.
add_library(mrdocs-plugin-api-link-test MODULE LinkProbe.cpp)
target_link_libraries(mrdocs-plugin-api-link-test PRIVATE mrdocs)
target_compile_features(mrdocs-plugin-api-link-test PRIVATE cxx_std_23)
# The header is compiled into every plugin, so it has to stay clean under the
# strict warnings a C++ plugin may enable.
target_compile_options(mrdocs-plugin-api-link-test PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wold-style-cast -Werror>
    $<$<CXX_COMPILER_ID:GNU>:-Wuseless-cast>)

# The executable exports the functions of the plugin C API and nothing else:
# the functions the header declares are there, and no C++, LLVM or Clang
# symbol is. It runs on Linux and macOS, where readelf or nm exists; a Windows executable
# exports what is marked for export. A shared or sanitized build keeps the
# default export table, so there is nothing narrowed to check.
get_target_property(MRDOCS_NARROW_EXPORTS mrdocs MRDOCS_NARROW_EXPORTS)
if (UNIX AND MRDOCS_NARROW_EXPORTS)
    if (APPLE)
        find_program(MRDOCS_NM NAMES nm llvm-nm)
    else ()
        find_program(MRDOCS_NM NAMES readelf llvm-readelf)
    endif ()
    if (MRDOCS_NM)
        add_test(NAME mrdocs-plugin-exports
            COMMAND ${CMAKE_COMMAND}
                "-DNM=${MRDOCS_NM}"
                "-DEXE=$<TARGET_FILE:mrdocs>"
                "-DHEADER=${PROJECT_SOURCE_DIR}/include/mrdocs/plugin.h"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckExports.cmake")
    else ()
        message(STATUS "No symbol lister (nm or readelf) found: mrdocs-plugin-exports is not registered")
    endif ()
endif ()

# The refusal to load a plugin built for an ABI newer than this MrDocs
# provides. Every other plugin in the tree reports the ABI its own header
# targets, so nothing else can reach that path.
add_library(mrdocs-plugin-foreign-version MODULE ForeignVersion.cpp)
target_link_libraries(mrdocs-plugin-foreign-version PRIVATE mrdocs)
target_compile_features(mrdocs-plugin-foreign-version PRIVATE cxx_std_23)

# The library goes into an addon root of its own, so that it is offered to
# the run below and to no other test. The generator expression keeps CMake
# from appending a per-configuration subdirectory, which would put the
# library somewhere the test cannot name.
set(MRDOCS_FOREIGN_PLUGIN_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/addons")
set_target_properties(mrdocs-plugin-foreign-version PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${MRDOCS_FOREIGN_PLUGIN_ADDONS}/plugins>")

add_test(NAME mrdocs-plugin-foreign-version-refused
    COMMAND
        mrdocs
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
        "--output=${CMAKE_CURRENT_BINARY_DIR}/refused-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${MRDOCS_FOREIGN_PLUGIN_ADDONS}"
)
set_tests_properties(mrdocs-plugin-foreign-version-refused PROPERTIES
    PASS_REGULAR_EXPRESSION
        "needs a newer MrDocs \\(ABI [0-9]+, this MrDocs provides [0-9]+\\)")

# The refusal to load a library that refers to a function of the plugin API
# that this MrDocs lacks. The loader resolves symbols when it opens a library,
# so the failure has to name the symbol at load time instead of crashing at
# the first call, and has to point at a newer MrDocs. A Windows link cannot
# leave a symbol undefined, so the library cannot be built there.
if (NOT WIN32)
    add_library(mrdocs-plugin-undefined-symbol MODULE UndefinedSymbol.cpp)
    target_link_libraries(mrdocs-plugin-undefined-symbol PRIVATE mrdocs)
    target_compile_features(mrdocs-plugin-undefined-symbol PRIVATE cxx_std_23)
    if (APPLE)
        target_link_options(mrdocs-plugin-undefined-symbol PRIVATE
            -undefined dynamic_lookup)
    endif ()

    set(MRDOCS_UNDEFINED_PLUGIN_ADDONS
        "${CMAKE_CURRENT_BINARY_DIR}/undefined-symbol-addons")
    set_target_properties(mrdocs-plugin-undefined-symbol PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY
            "$<1:${MRDOCS_UNDEFINED_PLUGIN_ADDONS}/plugins>")

    add_test(NAME mrdocs-plugin-undefined-symbol-refused
        COMMAND
            mrdocs
            "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
            "--output=${CMAKE_CURRENT_BINARY_DIR}/undefined-symbol-output"
            ${MRDOCS_BUILTIN_DIR_ARGS}
            "--addons-supplemental=${MRDOCS_UNDEFINED_PLUGIN_ADDONS}"
    )
    set_tests_properties(mrdocs-plugin-undefined-symbol-refused PROPERTIES
        PASS_REGULAR_EXPRESSION
            "could not be loaded:.*mrdocs_function_of_a_later_abi.*may need a newer MrDocs")
endif ()

# The Windows search order for the libraries a plugin depends on. The helper
# lives in a directory that is not the plugins directory and not the one
# holding the executable, and that directory is put in PATH. Plain
# LoadLibrary would find the helper there and load the plugin; the loader's
# search flags leave PATH out, so the plugin has to be refused at load time.
# The test matches the "could not be loaded" message and not the localized
# system text. This also runs the loader's error reporting on Windows. The success case, a helper next to the plugin, has
# to wait until libraries in plugins/ that are not plugins are skipped.
if (WIN32)
    set(MRDOCS_DEPENDENT_PLUGIN_ADDONS
        "${CMAKE_CURRENT_BINARY_DIR}/dependent-addons")
    set(MRDOCS_DEPENDENCY_HELPER_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/dependency-helper")

    add_library(mrdocs-plugin-dependency-helper SHARED DependencyHelper.cpp)
    set_target_properties(mrdocs-plugin-dependency-helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<1:${MRDOCS_DEPENDENCY_HELPER_DIR}>")

    add_library(mrdocs-plugin-dependent MODULE DependentPlugin.cpp)
    target_link_libraries(mrdocs-plugin-dependent PRIVATE
        mrdocs mrdocs-plugin-dependency-helper)
    target_compile_features(mrdocs-plugin-dependent PRIVATE cxx_std_23)
    set_target_properties(mrdocs-plugin-dependent PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY
            "$<1:${MRDOCS_DEPENDENT_PLUGIN_ADDONS}/plugins>")

    add_test(NAME mrdocs-plugin-dependency-not-on-path
        COMMAND
            mrdocs
            "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
            "--output=${CMAKE_CURRENT_BINARY_DIR}/dependent-output"
            ${MRDOCS_BUILTIN_DIR_ARGS}
            "--addons-supplemental=${MRDOCS_DEPENDENT_PLUGIN_ADDONS}"
    )
    set(MRDOCS_PATH_WITH_HELPER "$ENV{PATH}")
    string(REPLACE ";" "\\;" MRDOCS_PATH_WITH_HELPER
        "${MRDOCS_DEPENDENCY_HELPER_DIR};${MRDOCS_PATH_WITH_HELPER}")
    set_tests_properties(mrdocs-plugin-dependency-not-on-path PROPERTIES
        ENVIRONMENT "PATH=${MRDOCS_PATH_WITH_HELPER}"
        PASS_REGULAR_EXPRESSION
            "the plugin \".*mrdocs-plugin-dependent.*\" could not be loaded:")
endif ()

# The C++ wrapper of the plugin API, compiled into plugins of its own.
add_subdirectory(wrapper)
