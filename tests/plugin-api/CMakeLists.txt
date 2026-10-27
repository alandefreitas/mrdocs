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

# The executable exports the MrDocs API and none of LLVM, Clang, Lua or
# JerryScript. It runs on Linux and macOS, where nm exists; a Windows
# executable exports what is marked for export. A shared or sanitized build
# keeps the default export table, so there is nothing narrowed to check.
get_target_property(MRDOCS_EXPORTS_NARROWED mrdocs MRDOCS_NARROW_EXPORTS)
if (UNIX AND MRDOCS_EXPORTS_NARROWED)
    find_program(MRDOCS_NM NAMES nm llvm-nm)
    find_program(MRDOCS_CXXFILT NAMES c++filt llvm-cxxfilt)
    if (MRDOCS_NM AND MRDOCS_CXXFILT)
        add_test(NAME mrdocs-plugin-exports
            COMMAND ${CMAKE_COMMAND}
                "-DNM=${MRDOCS_NM}"
                "-DCXXFILT=${MRDOCS_CXXFILT}"
                "-DEXE=$<TARGET_FILE:mrdocs>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckExports.cmake")
    else ()
        message(STATUS "No nm or c++filt found: mrdocs-plugin-exports is not registered")
    endif ()
endif ()

# Register a test that runs MrDocs with the addon root `addons` and passes
# only if the run fails with output matching `expected`.
function(mrdocs_add_plugin_failure_test name addons expected)
    mrdocs_add_checked_run(${name} failure "${expected}"
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
        "--output=${CMAKE_CURRENT_BINARY_DIR}/${name}-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${addons}")
endfunction()

# A module library built and linked the way a plugin is, and then loaded, so
# that both building and loading it are the test: see LinkProbe.cpp for what
# that catches. It installs nothing: it has no entry point, so the run is
# refused for lacking the build id, and that refusal is only reached if the
# loader resolved every MrDocs symbol the library names. A symbol the tool
# does not export fails earlier, with an undefined-symbol message, and the
# test fails.
set(MRDOCS_LINK_PROBE_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/link-probe")
mrdocs_add_plugin_module(mrdocs-plugin-api-link-test LinkProbe.cpp
    ADDONS "${MRDOCS_LINK_PROBE_ADDONS}")
mrdocs_add_plugin_failure_test(mrdocs-plugin-api-link-test-loads
    "${MRDOCS_LINK_PROBE_ADDONS}"
    "does not export mrdocs_plugin_build_id")

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

mrdocs_add_refused_plugin(foreign-build-id ForeignBuildId.cpp
    "was built for MrDocs 1999.1.1\\+0123456789ab, and this is ")
mrdocs_add_refused_plugin(foreign-tag ForeignTag.cpp
    "was built with foreign-stdlib, and this MrDocs with ")
mrdocs_add_refused_plugin(null-build-id NullBuildId.cpp
    "reports no build id")
mrdocs_add_refused_plugin(null-tag NullTag.cpp
    "reports no standard library")
mrdocs_add_refused_plugin(missing-main MissingMain.cpp
    "does not export mrdocs_plugin_main")

# An entry point that fails with a message, and one that fails without: the
# first run names the reason, the second only says the plugin failed.
mrdocs_add_refused_plugin(main-error MainError.cpp
    "the plugin \".*mrdocs-plugin-main-error.*\" failed: the entry point gave up")
mrdocs_add_refused_plugin(main-false MainFalse.cpp
    "the plugin \".*mrdocs-plugin-main-false.*\" reported a failure")

# A library with none of the entry points, as a dependency of a plugin would
# be if it were put in the plugins directory instead of in the subdirectory
# named after the plugin.
mrdocs_add_refused_plugin(not-a-plugin NotAPlugin.cpp
    "the plugin \".*mrdocs-plugin-not-a-plugin.*\" does not export mrdocs_plugin_build_id")

# The same generator id in two plugins: the one that loads second is refused.
# The libraries load in name order.
set(MRDOCS_DUPLICATE_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/duplicate-generator")
foreach (tag a b)
    mrdocs_add_plugin_module(mrdocs-plugin-duplicate-${tag} IdGenerator.cpp
        ADDONS "${MRDOCS_DUPLICATE_ADDONS}")
    target_compile_definitions(mrdocs-plugin-duplicate-${tag} PRIVATE
        "MRDOCS_TEST_GENERATOR_ID=\"duplicate\"")
endforeach ()
mrdocs_add_plugin_failure_test(mrdocs-plugin-duplicate-generator-refused
    "${MRDOCS_DUPLICATE_ADDONS}"
    "the plugin \".*mrdocs-plugin-duplicate-b.*\" failed: generator id=\"duplicate\" already exists")

# A generator id that ships with MrDocs.
mrdocs_add_refused_plugin(builtin-generator IdGenerator.cpp
    "the plugin \".*mrdocs-plugin-builtin-generator.*\" failed: generator id=\"xml\" already exists")
target_compile_definitions(mrdocs-plugin-builtin-generator PRIVATE
    "MRDOCS_TEST_GENERATOR_ID=\"xml\"")

# A plugins entry that is a regular file, not a directory. It holds no
# plugin, so the run goes on after saying the directory could not be read.
set(MRDOCS_FILE_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/plugins-is-a-file")
file(MAKE_DIRECTORY "${MRDOCS_FILE_ADDONS}")
file(WRITE "${MRDOCS_FILE_ADDONS}/plugins" "not a directory\n")
mrdocs_add_checked_run(mrdocs-plugin-plugins-is-a-file success
    "The plugin directory \".*plugins-is-a-file/plugins\" could not be read"
    "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/mrdocs.yml"
    --generator=noop
    "--output=${CMAKE_CURRENT_BINARY_DIR}/mrdocs-plugin-plugins-is-a-file-output"
    ${MRDOCS_BUILTIN_DIR_ARGS}
    "--addons-supplemental=${MRDOCS_FILE_ADDONS}")

# A symbolic link in the plugins directory whose target is missing, as one
# left by a build tree that was moved or cleaned. It has a library name, so
# it is a plugin that cannot be loaded and the run names it. Creating a link
# needs a privilege on Windows, and file(CREATE_LINK) needs CMake 3.14, so the
# case is left out there and below that.
if (NOT WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.14)
    set(MRDOCS_DANGLING_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/dangling-link")
    file(MAKE_DIRECTORY "${MRDOCS_DANGLING_ADDONS}/plugins")
    file(CREATE_LINK "no-such-directory/missing-plugin.so"
        "${MRDOCS_DANGLING_ADDONS}/plugins/mrdocs-plugin-dangling.so" SYMBOLIC)
    mrdocs_add_plugin_failure_test(mrdocs-plugin-dangling-link-refused
        "${MRDOCS_DANGLING_ADDONS}"
        "the plugin \".*mrdocs-plugin-dangling\\.so\" could not be loaded")
endif ()

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

    mrdocs_add_plugin_failure_test(mrdocs-plugin-dependency-not-on-path-refused
        "${dependent_addons}"
        "the plugin \".*mrdocs-plugin-dependent.*\" could not be loaded:")
    file(TO_NATIVE_PATH "${dependency_helper_dir}" native_helper_dir)
    set(path_with_helper "$ENV{PATH}")
    string(REPLACE ";" "\\;" path_with_helper
        "${native_helper_dir};${path_with_helper}")
    set_tests_properties(mrdocs-plugin-dependency-not-on-path-refused PROPERTIES
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

# The same plugin loaded three times into one process, into two registries and
# then into the first again. The tool loads plugins once per run, so a program
# of its own does it: it is linked the way the tool is, and the plugin links
# it in the tool's place. The plugin's entry point has to run once, and the
# transform it installed has to reach both registries.
set(MRDOCS_RELOAD_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/reload-addons")
add_executable(mrdocs-plugin-reload-host ReloadHost.cpp)
mrdocs_make_plugin_host(mrdocs-plugin-reload-host)
target_include_directories(mrdocs-plugin-reload-host
    PRIVATE "${PROJECT_SOURCE_DIR}/src")
mrdocs_add_plugin_module(mrdocs-plugin-reload ReloadPlugin.cpp
    ADDONS "${MRDOCS_RELOAD_ADDONS}" HOST mrdocs-plugin-reload-host)
add_test(NAME mrdocs-plugin-reload
    COMMAND mrdocs-plugin-reload-host "${MRDOCS_RELOAD_ADDONS}")

# Two plugins that each export a function of the same name, which return
# different values. The loader opens a library so that its symbols stay its
# own, so the entry point of each one calls its own function; had the symbols
# of the first been made global, the second would call that one and be
# refused. macOS and Windows bind names per library, so the run says nothing
# there.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MRDOCS_LOCAL_SYMBOLS_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/local-symbols")
    foreach (tag a b)
        mrdocs_add_plugin_module(mrdocs-plugin-local-symbols-${tag}
            LocalSymbols.cpp LocalSymbolsTag.cpp
            ADDONS "${MRDOCS_LOCAL_SYMBOLS_ADDONS}")
        target_compile_definitions(mrdocs-plugin-local-symbols-${tag} PRIVATE
            "MRDOCS_TEST_TAG=\"${tag}\"")
    endforeach ()
    mrdocs_add_checked_run(mrdocs-plugin-local-symbols success
        "Loaded plugin \".*mrdocs-plugin-local-symbols-b"
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/mrdocs.yml"
        --generator=noop
        "--output=${CMAKE_CURRENT_BINARY_DIR}/mrdocs-plugin-local-symbols-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${MRDOCS_LOCAL_SYMBOLS_ADDONS}"
        --log-level=info)
endif ()
