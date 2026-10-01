#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Docs-tooling. The generator is a run-target (usable any time); the
# up-to-date check is a ctest test (CI "Utility Tests").
add_custom_target(generate-bootstrap-options
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_bootstrap_options.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Regenerate the bootstrap-options docs partial")

add_custom_target(generate-plugin-api-reference
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_plugin_api_reference.py --update
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Regenerate the plugin C API reference docs partial")

if (MRDOCS_BUILD_TESTS)
    add_test(NAME utils-bootstrap-options-check
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_bootstrap_options.py --check
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

    # Rejects a header with a declaration that has no "@since ABI n" line or
    # that is not recorded in tests/plugin-api/abi/AbiBaseline.c, a function,
    # callback field or callback type that does not name MRDOCS_PLUGIN_CALL, a
    # function that tests/plugin-api/LinkProbe.cpp does not call, and a
    # reference table that no longer matches include/mrdocs/plugin.h.
    add_test(NAME utils-plugin-api-reference-check
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_plugin_api_reference.py --check
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

    # Proves the check above fails on each kind of mistake it exists to catch,
    # with cases derived from the real header.
    add_test(NAME utils-plugin-api-reference-selftest
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_plugin_api_reference.py --self-test
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif ()
