#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# The C++ wrapper of the plugin API (mrdocs/plugin.hpp), compiled into plugins
# the way a plugin author compiles it. WrapperPlugin.cpp reads and writes
# values, iterates, keeps a reference across callbacks and fails in several
# ways; InitError.cpp has an entry point that throws.

# The wrapper is C++17, and the header is compiled into every plugin, so the
# plugins are built as C++17 with the strict warnings a plugin may enable.
function (mrdocs_add_wrapper_plugin target source addons)
    add_library(${target} MODULE ${source})
    target_link_libraries(${target} PRIVATE mrdocs::plugin)
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic -Wold-style-cast -Wconversion -Wsign-conversion -Werror>
        $<$<CXX_COMPILER_ID:GNU>:-Wuseless-cast>)
    # The library goes into an addon root of its own, so that it is offered to
    # the runs of this directory and to no other test. The generator expression
    # keeps CMake from appending a per-configuration subdirectory, which would
    # put the library somewhere the test cannot name.
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "$<1:${addons}/plugins>")
endfunction ()

set(MRDOCS_WRAPPER_ADDONS "${CMAKE_CURRENT_BINARY_DIR}/addons")
set(MRDOCS_WRAPPER_INIT_ERROR_ADDONS
    "${CMAKE_CURRENT_BINARY_DIR}/init-error-addons")
mrdocs_add_wrapper_plugin(mrdocs-plugin-wrapper
    WrapperPlugin.cpp "${MRDOCS_WRAPPER_ADDONS}")
mrdocs_add_wrapper_plugin(mrdocs-plugin-wrapper-init-error
    InitError.cpp "${MRDOCS_WRAPPER_INIT_ERROR_ADDONS}")

# A plugin that defines MRDOCS_PLUGIN_ABI_TARGET as 1: the wrapper has to
# compile for the lowest ABI, whatever the newest one is. Building it is the
# test, and its addon root is not offered to any run.
mrdocs_add_wrapper_plugin(mrdocs-plugin-wrapper-target-one
    TargetOne.cpp "${CMAKE_CURRENT_BINARY_DIR}/target-one-addons")

# A run of the tool with the wrapper plugin: `mrdocs_add_wrapper_run(<name>
# <generator> [CONFIG <file>] [ADDONS <dir>] [FAILS <regex>])` registers the
# ctest entry <name> and the properties that go with it. With FAILS the run
# has to stop with the status of a refusal and print a message that matches
# <regex>; the exit status counts, because a run that only prints the message
# and goes on would otherwise pass.
function (mrdocs_add_wrapper_run name generator)
    cmake_parse_arguments(ARG "" "CONFIG;ADDONS;FAILS" "" ${ARGN})
    if (NOT ARG_CONFIG)
        set(ARG_CONFIG mrdocs.yml)
    endif ()
    if (NOT ARG_ADDONS)
        set(ARG_ADDONS "${MRDOCS_WRAPPER_ADDONS}")
    endif ()
    set(arguments
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/project/${ARG_CONFIG}"
        --generator=${generator}
        "--output=${CMAKE_CURRENT_BINARY_DIR}/${name}-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${ARG_ADDONS}"
        --log-level=warn)
    if (ARG_FAILS)
        mrdocs_add_checked_run(${name} failure "${ARG_FAILS}" ${arguments})
    else ()
        add_test(NAME ${name} COMMAND mrdocs ${arguments})
    endif ()
endfunction ()

# The generator reads and writes values and records what it saw. A failed
# check inside it fails the run, and the second entry compares the record.
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-values wrapper-values)
set_tests_properties(mrdocs-plugin-wrapper-values PROPERTIES
    FIXTURES_SETUP mrdocs-plugin-wrapper-values-output)
add_test(NAME mrdocs-plugin-wrapper-values-compare
    COMMAND ${CMAKE_COMMAND} -E compare_files
        "${CMAKE_CURRENT_BINARY_DIR}/mrdocs-plugin-wrapper-values-output/values.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/values.txt")
set_tests_properties(mrdocs-plugin-wrapper-values-compare PROPERTIES
    FIXTURES_REQUIRED mrdocs-plugin-wrapper-values-output)

# The wrapper compiled as C++20, where Array and Object are ranges. Nothing
# links or runs: building the object is the test.
add_library(mrdocs-plugin-wrapper-ranges OBJECT Ranges.cpp)
target_link_libraries(mrdocs-plugin-wrapper-ranges PRIVATE mrdocs::plugin)
target_compile_features(mrdocs-plugin-wrapper-ranges PRIVATE cxx_std_20)

# An exception that leaves a callback is the error of the run, whatever its
# type. A failed call to MrDocs throws an Error that names the function and
# the status. When a callback sets an error and then throws, the first
# message is the one the run reports.
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-throws-std wrapper-throws-std
    FAILS "the generator gave up")
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-throws-other wrapper-throws-other
    FAILS "unexpected exception")
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-throws-empty wrapper-throws-empty
    FAILS "an exception with no message")
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-bad-call wrapper-bad-call
    FAILS "mrdocs_get_string_utf8 failed: type_mismatch")
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-host-reason wrapper-host-reason
    FAILS "mrdocs_object_set failed: read_only: cannot set \"a\": this is a read-only view")
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-refusal wrapper-refusal
    FAILS "Value::set failed: invalid_arg: the text is null")
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-first-error wrapper-first-error
    FAILS "the first message")
set_tests_properties(mrdocs-plugin-wrapper-first-error PROPERTIES
    FAIL_REGULAR_EXPRESSION "the second message")

# A message at the error level makes the run exit with a failure status once
# it ends, though the generator returns normally.
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-logged-error wrapper-logged-error
    FAILS "an error logged by the generator")

# The same holds for a transform, which runs before any generator.
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-transform-throws wrapper-values
    CONFIG failing-transform.yml FAILS "the transform gave up")

# And for the entry point defined with MRDOCS_PLUGIN_INIT_CPP.
mrdocs_add_wrapper_run(mrdocs-plugin-wrapper-init-throws wrapper-values
    ADDONS "${MRDOCS_WRAPPER_INIT_ERROR_ADDONS}"
    FAILS "the entry point gave up")
