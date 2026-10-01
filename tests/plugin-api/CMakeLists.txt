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
target_link_libraries(mrdocs-plugin-api-link-test PRIVATE mrdocs::plugin)
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

# `mrdocs_add_checked_run(<name> <success|failure> <regex> <argument>...)`
# registers the ctest entry <name>, which runs the `mrdocs` tool with the
# arguments and passes if its exit status is zero (success) or 1, the status
# of a refusal (failure), and its output matches <regex>. PASS_REGULAR_EXPRESSION
# alone would ignore the exit status, which is what tells a run that stopped
# from one that went on. The script path is captured here because
# CMAKE_CURRENT_FUNCTION_LIST_DIR needs CMake 3.17 and the minimum is 3.13.
set(MRDOCS_RUN_CHECKED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/RunChecked.cmake")
function (mrdocs_add_checked_run name outcome regex)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            "-DOUTCOME=${outcome}"
            "-DREGEX=${regex}"
            -P "${MRDOCS_RUN_CHECKED_SCRIPT}"
            --
            $<TARGET_FILE:mrdocs>
            ${ARGN})
endfunction ()

# The refusal to load a plugin built for an ABI newer than this MrDocs
# provides. Every other plugin in the tree reports the ABI its own header
# targets, so nothing else can reach that path.
add_library(mrdocs-plugin-foreign-version MODULE ForeignVersion.cpp)
target_link_libraries(mrdocs-plugin-foreign-version PRIVATE mrdocs::plugin)
target_compile_features(mrdocs-plugin-foreign-version PRIVATE cxx_std_23)

# The library goes into an addon root of its own, so that it is offered to
# the run below and to no other test. The generator expression keeps CMake
# from appending a per-configuration subdirectory, which would put the
# library somewhere the test cannot name.
set(MRDOCS_FOREIGN_PLUGIN_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/addons")
set_target_properties(mrdocs-plugin-foreign-version PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${MRDOCS_FOREIGN_PLUGIN_ADDONS}/plugins>")

mrdocs_add_checked_run(mrdocs-plugin-foreign-version-refused failure
    "needs a newer MrDocs \\(ABI [0-9]+, this MrDocs provides [0-9]+\\)"
    "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
    "--output=${CMAKE_CURRENT_BINARY_DIR}/refused-output"
    ${MRDOCS_BUILTIN_DIR_ARGS}
    "--addons-supplemental=${MRDOCS_FOREIGN_PLUGIN_ADDONS}")

# The refusal to load a library that refers to a function of the plugin API
# that this MrDocs lacks. The loader resolves symbols when it opens a library,
# so the failure has to name the symbol at load time instead of crashing at
# the first call, and has to point at a newer MrDocs. A Windows link cannot
# leave a symbol undefined, so the library cannot be built there. On macOS
# mrdocs::plugin tells the linker to leave it undefined.
if (NOT WIN32)
    add_library(mrdocs-plugin-undefined-symbol MODULE UndefinedSymbol.cpp)
    target_link_libraries(mrdocs-plugin-undefined-symbol PRIVATE mrdocs::plugin)
    target_compile_features(mrdocs-plugin-undefined-symbol PRIVATE cxx_std_23)

    set(MRDOCS_UNDEFINED_PLUGIN_ADDONS
        "${CMAKE_CURRENT_BINARY_DIR}/undefined-symbol-addons")
    set_target_properties(mrdocs-plugin-undefined-symbol PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY
            "$<1:${MRDOCS_UNDEFINED_PLUGIN_ADDONS}/plugins>")

    mrdocs_add_checked_run(mrdocs-plugin-undefined-symbol-refused failure
        "could not be loaded:.*mrdocs_function_of_a_later_abi.*may need a newer MrDocs"
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
        "--output=${CMAKE_CURRENT_BINARY_DIR}/undefined-symbol-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${MRDOCS_UNDEFINED_PLUGIN_ADDONS}")
endif ()

# The Windows search order for the libraries a plugin depends on. The helper
# lives in a directory that is not plugins/lib and not the one holding the
# executable, and that directory is put in PATH. Plain LoadLibrary would find
# the helper there and load the plugin; the loader's search flags leave PATH
# out, so the plugin has to be refused at load time. The test matches the
# "could not be loaded" message and not the localized system text. This also
# runs the loader's error reporting on Windows. The test where the helper is
# in plugins/lib, and has to be found, is in c/CMakeLists.txt.
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
        mrdocs::plugin mrdocs-plugin-dependency-helper)
    target_compile_features(mrdocs-plugin-dependent PRIVATE cxx_std_23)
    set_target_properties(mrdocs-plugin-dependent PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY
            "$<1:${MRDOCS_DEPENDENT_PLUGIN_ADDONS}/plugins>")

    set(MRDOCS_PATH_WITH_HELPER "$ENV{PATH}")
    string(REPLACE ";" "\\;" MRDOCS_PATH_WITH_HELPER
        "${MRDOCS_DEPENDENCY_HELPER_DIR};${MRDOCS_PATH_WITH_HELPER}")
    mrdocs_add_checked_run(mrdocs-plugin-dependency-not-on-path failure
        "the plugin \".*mrdocs-plugin-dependent.*\" could not be loaded:"
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
        "--output=${CMAKE_CURRENT_BINARY_DIR}/dependent-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${MRDOCS_DEPENDENT_PLUGIN_ADDONS}")
    set_tests_properties(mrdocs-plugin-dependency-not-on-path PROPERTIES
        ENVIRONMENT "PATH=${MRDOCS_PATH_WITH_HELPER}")
endif ()

# `mrdocs_add_c_plugin(<target> <addons> <source>...)` builds a module library
# written in C11, the way a plugin author builds one, into the `plugins`
# directory of the addon root <addons>. The root is offered to the runs that
# name it and to no other test. The generator expression keeps CMake from
# appending a per-configuration subdirectory, which would put the library
# somewhere the test cannot name.
function (mrdocs_add_c_plugin target addons)
    add_library(${target} MODULE ${ARGN})
    target_link_libraries(${target} PRIVATE mrdocs::plugin)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        LIBRARY_OUTPUT_DIRECTORY "$<1:${addons}/plugins>")
    # Plain C compiled with the strict warnings a plugin may enable.
    target_compile_options(${target} PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic -Wmissing-prototypes -Wconversion -Werror>)
endfunction ()

# A plugin written in C, a plugin with a dependency in plugins/lib, a plugins
# directory that is a file, and (Linux only) two plugins that export a function
# of the same name and each call their own.
add_subdirectory(c)

# Libraries MrDocs has to refuse: a library without the entry points, a plugin
# that reports ABI 0, an entry point that is missing or fails, a generator id
# that is taken, or (not on Windows) a symbolic link with a library name whose
# target is missing.
add_subdirectory(refusals)

# The C++ wrapper of the plugin API, compiled into plugins of its own.
add_subdirectory(wrapper)
