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

# The in-tree directory flags, with | between them for ExpectFailure.cmake,
# which splits them again: a ; would split the -D definition carrying them.
string(REPLACE ";" "|" MRDOCS_PLUGIN_TEST_DIR_ARGS "${MRDOCS_BUILTIN_DIR_ARGS}")

# Register a test that runs MrDocs with the addon root `addons` and passes
# only if the run fails with output matching `expected`.
function(mrdocs_add_plugin_failure_test name addons expected)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            "-DMRDOCS=$<TARGET_FILE:mrdocs>"
            "-DCONFIG=${CMAKE_CURRENT_SOURCE_DIR}/mrdocs.yml"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}-output"
            "-DADDONS=${addons}"
            "-DBUILTIN_ARGS=${MRDOCS_PLUGIN_TEST_DIR_ARGS}"
            "-DEXPECTED=${expected}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectFailure.cmake")
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

# The Windows search order for the libraries a plugin depends on. The helper
# lives in a directory that is not the plugins directory, not the
# subdirectory named after the plugin and not the one holding the executable,
# and that directory is put in PATH. Plain LoadLibrary would find the helper
# there and load the plugin; the loader's search flags leave PATH out, so the
# plugin has to be refused at load time. The test matches the "could not be
# loaded" message and not the localized system text. This also runs the
# loader's error reporting on Windows.
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

    set(dependent_expected
        "the plugin \".*mrdocs-plugin-dependent.*\" could not be loaded:")
    mrdocs_add_plugin_failure_test(mrdocs-plugin-dependency-not-on-path
        "${dependent_addons}" "${dependent_expected}")
    set(path_with_helper "$ENV{PATH}")
    string(REPLACE ";" "\\;" path_with_helper
        "${dependency_helper_dir};${path_with_helper}")
    set_tests_properties(mrdocs-plugin-dependency-not-on-path PROPERTIES
        ENVIRONMENT "PATH=${path_with_helper}")

    # The same plugin, loaded from an addon root whose path is not ASCII: on
    # Windows, a path read in the ANSI code page names another directory.
    add_test(NAME mrdocs-plugin-non-ascii-path-fails
        COMMAND ${CMAKE_COMMAND}
            "-DMRDOCS=$<TARGET_FILE:mrdocs>"
            "-DFROM=${dependent_addons}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/non-ascii-path"
            "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DBUILTIN_ARGS=${MRDOCS_PLUGIN_TEST_DIR_ARGS}"
            "-DEXPECTED=${dependent_expected}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/NonAsciiPath.cmake")
    set_tests_properties(mrdocs-plugin-non-ascii-path-fails PROPERTIES
        ENVIRONMENT "PATH=${path_with_helper}")
endif ()
