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

# A module library built and linked the way a plugin is, so that building it
# is the test: see LinkProbe.cpp for what that catches and what it does not.
# It is deliberately not registered with ctest, since it runs nothing.
mrdocs_add_plugin_module(mrdocs-plugin-api-link-test LinkProbe.cpp)

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

# Register a test that runs MrDocs with the addon root `addons` and passes
# only if the run fails with output matching `expected`.
function(mrdocs_add_plugin_failure_test name addons expected)
    mrdocs_add_checked_run(${name} failure "${expected}"
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
        "--output=${CMAKE_CURRENT_BINARY_DIR}/${name}-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${addons}")
endfunction()

# A plugin MrDocs has to refuse, built from `source` into an addon root of
# its own, so that it is offered to its own test and to no other. Every
# other plugin in the tree reports what its headers and compiler define, so
# nothing else reaches these paths.
function(mrdocs_add_refused_plugin name source expected)
    set(addons "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    mrdocs_add_plugin_module(mrdocs-plugin-${name} ${source} ADDONS "${addons}")
    mrdocs_add_plugin_failure_test(
        mrdocs-plugin-${name}-refused "${addons}" "${expected}")
endfunction()

mrdocs_add_refused_plugin(foreign-version ForeignVersion.cpp
    "was built against version")
mrdocs_add_refused_plugin(foreign-tag ForeignTag.cpp
    "was built with .foreign-toolchain.")
mrdocs_add_refused_plugin(missing-main MissingMain.cpp
    "does not export mrdocs_plugin_main")

# The refusal to load a library that refers to a symbol nothing provides.
# The loader resolves symbols when it opens a library, so the failure has to
# name the symbol at load time instead of crashing at the first call. A
# Windows link cannot leave a symbol undefined, so the library cannot be
# built there.
if (NOT WIN32)
    mrdocs_add_refused_plugin(undefined-symbol UndefinedSymbol.cpp
        "could not be loaded:.*mrdocs_test_symbol_nothing_provides")
    if (APPLE)
        target_link_options(mrdocs-plugin-undefined-symbol PRIVATE
            -undefined dynamic_lookup)
    endif ()
endif ()

# The shared libraries a plugin depends on. The plugin DependentPlugin.cpp
# calls a helper library, and fails unless the helper returns its value.
#
# Windows: the helper lives in a directory that is not the plugins directory,
# not the subdirectory named after the plugin and not the one holding the
# executable, and that directory is put in PATH. Plain LoadLibrary would find
# the helper there and load the plugin; the loader's search flags leave PATH
# out, so the plugin has to be refused at load time. The test matches the
# "could not be loaded" message and not the localized system text. This also
# runs the loader's error reporting on Windows.
if (WIN32)
    set(dependent_addons "${CMAKE_CURRENT_BINARY_DIR}/dependent-addons")
    set(dependency_helper_dir "${CMAKE_CURRENT_BINARY_DIR}/dependency-helper")

    add_library(mrdocs-plugin-dependency-helper SHARED DependencyHelper.cpp)
    set_target_properties(mrdocs-plugin-dependency-helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<1:${dependency_helper_dir}>")

    mrdocs_add_plugin_module(mrdocs-plugin-dependent DependentPlugin.cpp
        ADDONS "${dependent_addons}")
    target_link_libraries(mrdocs-plugin-dependent
        PRIVATE mrdocs-plugin-dependency-helper)

    mrdocs_add_plugin_failure_test(mrdocs-plugin-dependency-not-on-path
        "${dependent_addons}"
        "the plugin \".*mrdocs-plugin-dependent.*\" could not be loaded:")
    file(TO_NATIVE_PATH "${dependency_helper_dir}" native_helper_dir)
    set(path_with_helper "$ENV{PATH}")
    string(REPLACE ";" "\\;" path_with_helper
        "${native_helper_dir};${path_with_helper}")
    set_tests_properties(mrdocs-plugin-dependency-not-on-path PROPERTIES
        ENVIRONMENT "PATH=${path_with_helper}")
endif ()

# Every platform: the helper sits in the subdirectory of `plugins` named after
# the plugin, where the loader never looks for plugins, and has to be found.
# The name of the plugin file has no prefix, so that the subdirectory is the
# same on every platform. On Linux and macOS the plugin finds the library
# through its own rpath, which is the only search path it has: the build rpath
# that CMake would add, naming the build directory, is turned off. On Windows
# the loader adds the subdirectory to the directories it searches while it
# loads the plugin. The entry point of the plugin fails unless the helper
# returns its value, so a run that loads the plugin shows the helper was
# found and called.
set(MRDOCS_FOUND_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/found-addons")
set(MRDOCS_FOUND_STEM mrdocs-plugin-dependent-found)

add_library(mrdocs-plugin-dependency-helper-found SHARED DependencyHelper.cpp)
set_target_properties(mrdocs-plugin-dependency-helper-found PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY
        "$<1:${MRDOCS_FOUND_ADDONS}/plugins/${MRDOCS_FOUND_STEM}>"
    RUNTIME_OUTPUT_DIRECTORY
        "$<1:${MRDOCS_FOUND_ADDONS}/plugins/${MRDOCS_FOUND_STEM}>")

mrdocs_add_plugin_module(${MRDOCS_FOUND_STEM} DependentPlugin.cpp
    ADDONS "${MRDOCS_FOUND_ADDONS}")
target_link_libraries(${MRDOCS_FOUND_STEM} PRIVATE
    mrdocs-plugin-dependency-helper-found)
set_target_properties(${MRDOCS_FOUND_STEM} PROPERTIES
    PREFIX ""
    BUILD_WITH_INSTALL_RPATH ON)
if (APPLE)
    set_target_properties(${MRDOCS_FOUND_STEM} PROPERTIES
        INSTALL_RPATH "@loader_path/${MRDOCS_FOUND_STEM}")
elseif (UNIX)
    set_target_properties(${MRDOCS_FOUND_STEM} PROPERTIES
        INSTALL_RPATH "$ORIGIN/${MRDOCS_FOUND_STEM}")
endif ()

mrdocs_add_checked_run(mrdocs-plugin-dependency-in-plugin-dir success
    "Loaded plugin \".*${MRDOCS_FOUND_STEM}"
    "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/mrdocs.yml"
    --generator=noop
    "--output=${CMAKE_CURRENT_BINARY_DIR}/mrdocs-plugin-dependency-in-plugin-dir-output"
    ${MRDOCS_BUILTIN_DIR_ARGS}
    "--addons-supplemental=${MRDOCS_FOUND_ADDONS}"
    --log-level=info)

# The same plugin and helper, copied to an addon root whose name is not ASCII:
# on Windows, a path read in the ANSI code page names another directory. The
# run has to load the plugin and find its helper, as the one above does.
# NonAsciiPath.cmake makes the copy and runs the tool, and names the copy in a
# configuration file, which MrDocs reads as UTF-8, because on Windows its
# command line arrives in the ANSI code page. The directory flags go to the
# script with | between them, since a ; would split the -D definition.
string(REPLACE ";" "|" MRDOCS_NON_ASCII_BUILTIN_ARGS "${MRDOCS_BUILTIN_DIR_ARGS}")
add_test(NAME mrdocs-plugin-dependency-non-ascii-root
    COMMAND ${CMAKE_COMMAND}
        "-DMRDOCS=$<TARGET_FILE:mrdocs>"
        "-DFROM=${MRDOCS_FOUND_ADDONS}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/non-ascii-root"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/project"
        "-DBUILTIN_ARGS=${MRDOCS_NON_ASCII_BUILTIN_ARGS}"
        "-DRUN_CHECKED=${MRDOCS_RUN_CHECKED_SCRIPT}"
        "-DREGEX=Loaded plugin \".*${MRDOCS_FOUND_STEM}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/NonAsciiPath.cmake")
