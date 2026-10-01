#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Each test suite is a separate executable and owns its own CMakeLists:
#   unit/       - public-API unit tests (mrdocs-unit-tests)
#   golden/     - reference-output harness (mrdocs-golden-tests)
#   plugin-api/ - the C header of the plugin API compiled as C, a check that
#                 the executable exports the functions of the C API and
#                 nothing else (mrdocs-plugin-exports), a module
#                 library that calls every function of it, where building
#                 them is the test (mrdocs-plugin-api-c-header,
#                 mrdocs-plugin-api-link-test), and libraries MrDocs has to
#                 refuse to load: one built for an ABI newer than MrDocs
#                 provides, one with an undefined symbol (Linux and macOS
#                 only), and one whose dependency is reachable only through
#                 PATH (Windows only); c/ holds a plugin written in C11 that
#                 reads the corpus and writes a file, a plugin whose shared
#                 library sits in plugins/lib, a plugins directory that
#                 is a file (a warning, not a refusal), and, on Linux only,
#                 two plugins that export a function of the same name, which
#                 each have to call their own; refusals/ holds the
#                 plugins MrDocs has to refuse by message: a library with
#                 neither entry point, one without mrdocs_plugin_init, one
#                 without mrdocs_plugin_abi_version, one that reports ABI 0,
#                 an entry point that fails with and without a message, a
#                 generator id that another plugin or a built-in generator
#                 took, and (not on Windows) a symbolic link with a library
#                 name whose target is missing; wrapper/ holds plugins written with the C++ wrapper
#                 (mrdocs/plugin.hpp), run to check values, iteration,
#                 references, the errors the wrapper turns exceptions
#                 into, and a message logged at the error level
add_subdirectory(unit)
add_subdirectory(golden)
# Benchmarks are custom targets, not CTest tests; see benchmarks/README.md.
add_subdirectory(benchmarks)
if (MRDOCS_BUILD_PLUGIN_MODULES)
    add_subdirectory(plugin-api)
endif ()

# Self-documentation test: run mrdocs over its own build file with the noop
# generator (warn-as-error toggled by the strict flag).
set(MRDOCS_SELF_DOC_OUTPUT "${CMAKE_BINARY_DIR}/docs/self-reference")
add_test(NAME mrdocs-self-doc
    COMMAND
        mrdocs
        "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
            "--config=${CMAKE_SOURCE_DIR}/docs/mrdocs.yml"
            "--output=${MRDOCS_SELF_DOC_OUTPUT}"
            ${MRDOCS_BUILTIN_DIR_ARGS}
            --generator=noop
            --log-level=debug
            $<$<BOOL:${MRDOCS_BUILD_STRICT_TESTS}>:--warn-as-error=true>
    )
