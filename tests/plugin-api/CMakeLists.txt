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
add_library(mrdocs-plugin-api-link-test MODULE LinkProbe.cpp)
target_link_libraries(mrdocs-plugin-api-link-test PRIVATE mrdocs)
target_compile_features(mrdocs-plugin-api-link-test PRIVATE cxx_std_23)

# The refusal to load a plugin built against another interface version.
# Every other plugin in the tree reports the version its own headers
# define, so nothing else can reach that path.
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
    PASS_REGULAR_EXPRESSION "was built against version")

# The refusal to load a library that refers to a symbol nothing provides.
# The loader resolves symbols when it opens a library, so the failure has to
# name the symbol at load time instead of crashing at the first call. A
# Windows link cannot leave a symbol undefined, so the library cannot be
# built there.
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
            "could not be loaded:.*mrdocs_test_symbol_nothing_provides")
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
