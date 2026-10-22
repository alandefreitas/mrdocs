#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# A plugin written in C11: CPlugin.c reads the corpus, registers a transform
# and a generator, and keeps a reference between the two callbacks.
set(MRDOCS_C_PLUGIN_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/addons")
mrdocs_add_c_plugin(mrdocs-plugin-c "${MRDOCS_C_PLUGIN_ADDONS}" CPlugin.c)

# `mrdocs_add_c_run(<name> <generator> [ADDONS <dir>] [OUTPUT <dir>]
# [ARGS <argument>...])` registers the ctest entry <name>, which runs the tool
# on the project of this directory.
function (mrdocs_add_c_run name generator)
    cmake_parse_arguments(ARG "" "ADDONS;OUTPUT" "ARGS" ${ARGN})
    if (NOT ARG_ADDONS)
        set(ARG_ADDONS "${MRDOCS_C_PLUGIN_ADDONS}")
    endif ()
    if (NOT ARG_OUTPUT)
        set(ARG_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${name}-output")
    endif ()
    add_test(NAME ${name}
        COMMAND
            mrdocs
            "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/mrdocs.yml"
            --generator=${generator}
            "--output=${ARG_OUTPUT}"
            ${MRDOCS_BUILTIN_DIR_ARGS}
            "--addons-supplemental=${ARG_ADDONS}"
            ${ARG_ARGS}
    )
endfunction ()

# The run succeeds only if the plugin loaded and wrote its file, and a second
# test compares the file with the one in the source tree.
mrdocs_add_c_run(mrdocs-plugin-c c-plugin
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/output" ARGS --log-level=warn)
set_tests_properties(mrdocs-plugin-c PROPERTIES
    FIXTURES_SETUP mrdocs-plugin-c-output)
add_test(NAME mrdocs-plugin-c-compare
    COMMAND ${CMAKE_COMMAND} -E compare_files
        "${CMAKE_CURRENT_BINARY_DIR}/output/c-plugin.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/c-plugin.txt")
set_tests_properties(mrdocs-plugin-c-compare PROPERTIES
    FIXTURES_REQUIRED mrdocs-plugin-c-output)

# A plugin that links a shared library kept in the subdirectory of `plugins`
# named after the plugin, where the loader never looks for plugins. The name of
# the plugin file has no prefix, so that the subdirectory is the same on every
# platform. On Linux and macOS the plugin finds the library through its own
# rpath, which is the only search path it has: the build rpath that CMake would
# add, naming the build directory, is turned off. On Windows the loader adds
# the subdirectory to the directories it searches while it loads the plugin.
# The entry point of the plugin fails unless the library returns its value, so
# a run that loads the plugin shows the library was found and called. The
# source files are the ones the PATH test in ../CMakeLists.txt builds, with
# the library in another place.
set(MRDOCS_DEPENDENT_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/dependent-addons")
set(MRDOCS_DEPENDENT_STEM mrdocs-plugin-dependent-found)

add_library(mrdocs-plugin-dependency-helper-found SHARED
    ../DependencyHelper.cpp)
set_target_properties(mrdocs-plugin-dependency-helper-found PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY
        "$<1:${MRDOCS_DEPENDENT_ADDONS}/plugins/${MRDOCS_DEPENDENT_STEM}>"
    RUNTIME_OUTPUT_DIRECTORY
        "$<1:${MRDOCS_DEPENDENT_ADDONS}/plugins/${MRDOCS_DEPENDENT_STEM}>")

mrdocs_add_plugin_module(${MRDOCS_DEPENDENT_STEM} ../DependentPlugin.cpp
    ADDONS "${MRDOCS_DEPENDENT_ADDONS}")
target_link_libraries(${MRDOCS_DEPENDENT_STEM} PRIVATE
    mrdocs-plugin-dependency-helper-found)
set_target_properties(${MRDOCS_DEPENDENT_STEM} PROPERTIES
    PREFIX ""
    BUILD_WITH_INSTALL_RPATH ON)
if (APPLE)
    set_target_properties(${MRDOCS_DEPENDENT_STEM} PROPERTIES
        INSTALL_RPATH "@loader_path/${MRDOCS_DEPENDENT_STEM}")
elseif (UNIX)
    set_target_properties(${MRDOCS_DEPENDENT_STEM} PROPERTIES
        INSTALL_RPATH "$ORIGIN/${MRDOCS_DEPENDENT_STEM}")
endif ()

mrdocs_add_checked_run(mrdocs-plugin-dependency-in-plugin-dir success
    "Loaded plugin \".*${MRDOCS_DEPENDENT_STEM}"
    "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/mrdocs.yml"
    --generator=noop
    "--output=${CMAKE_CURRENT_BINARY_DIR}/mrdocs-plugin-dependency-in-plugin-dir-output"
    ${MRDOCS_BUILTIN_DIR_ARGS}
    "--addons-supplemental=${MRDOCS_DEPENDENT_ADDONS}"
    --log-level=info)

# The same plugin and library, copied to an addon root whose name is not ASCII:
# on Windows, a path read in the ANSI code page names another directory. The
# run has to load the plugin and find its library, as the one above does.
# NonAsciiPath.cmake makes the copy and runs the tool, and names the copy in a
# configuration file, which MrDocs reads as UTF-8, because on Windows its
# command line arrives in the ANSI code page. The directory flags go to the
# script with | between them, since a ; would split the -D definition.
string(REPLACE ";" "|" MRDOCS_NON_ASCII_BUILTIN_ARGS "${MRDOCS_BUILTIN_DIR_ARGS}")
add_test(NAME mrdocs-plugin-dependency-non-ascii-root
    COMMAND ${CMAKE_COMMAND}
        "-DMRDOCS=$<TARGET_FILE:mrdocs>"
        "-DFROM=${MRDOCS_DEPENDENT_ADDONS}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/non-ascii-root"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/project"
        "-DBUILTIN_ARGS=${MRDOCS_NON_ASCII_BUILTIN_ARGS}"
        "-DRUN_CHECKED=${MRDOCS_RUN_CHECKED_SCRIPT}"
        "-DREGEX=Loaded plugin \".*${MRDOCS_DEPENDENT_STEM}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../NonAsciiPath.cmake")

# An addon root whose `plugins` is a file holds no plugins, and the run says
# the directory could not be read and goes on: the exit status is zero.
set(MRDOCS_PLUGINS_FILE_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/plugins-file-addons")
file(MAKE_DIRECTORY "${MRDOCS_PLUGINS_FILE_ADDONS}")
file(WRITE "${MRDOCS_PLUGINS_FILE_ADDONS}/plugins"
    "This is a file, where a directory of plugins is expected.\n")
mrdocs_add_checked_run(mrdocs-plugin-plugins-is-a-file success
    "The plugin directory \".*plugins\" could not be read"
    "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/mrdocs.yml"
    --generator=noop
    "--output=${CMAKE_CURRENT_BINARY_DIR}/mrdocs-plugin-plugins-is-a-file-output"
    ${MRDOCS_BUILTIN_DIR_ARGS}
    "--addons-supplemental=${MRDOCS_PLUGINS_FILE_ADDONS}"
    --log-level=warn)

# The loader keeps the symbols of a plugin to itself. Two libraries export a
# function of the same name, and the generator of the one that loads second
# runs: it has to call its own function. The libraries load in name order. With
# the symbols made global the second would bind to the function of the first.
# macOS and Windows bind names per library, so the run says nothing there.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MRDOCS_LOCAL_SYMBOLS_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/local-symbols-addons")
    foreach (tag a b)
        mrdocs_add_c_plugin(mrdocs-plugin-local-symbols-${tag}
            "${MRDOCS_LOCAL_SYMBOLS_ADDONS}" LocalSymbols.c LocalSymbolsTag.c)
        target_compile_definitions(mrdocs-plugin-local-symbols-${tag} PRIVATE
            "MRDOCS_TEST_TAG=\"${tag}\"")
    endforeach ()
    mrdocs_add_checked_run(mrdocs-plugin-local-symbols success
        "local-symbols: kept to itself"
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/mrdocs.yml"
        --generator=local-symbols-b
        "--output=${CMAKE_CURRENT_BINARY_DIR}/local-symbols-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${MRDOCS_LOCAL_SYMBOLS_ADDONS}"
        --log-level=info)
endif ()
