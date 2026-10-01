#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Libraries MrDocs has to refuse, each a small module of its own in an addon
# root of its own, and a ctest entry that runs the tool with that root and
# matches the diagnostic. The refusal happens while the tool starts up, which
# is before it reads a compilation database, so the project the runs name is
# never compiled; see ../mrdocs.yml. The refusal of a plugin built for a
# newer ABI and that of an undefined symbol are in ../CMakeLists.txt.

# `mrdocs_add_refusal(<name> <regex> <addons>)` registers the ctest entry
# mrdocs-plugin-refused-<name>, which runs the tool with the addon root
# <addons> and passes if the run fails with an output that matches <regex>.
function (mrdocs_add_refusal name regex addons)
    mrdocs_add_checked_run(mrdocs-plugin-refused-${name} failure "${regex}"
        "--config=${CMAKE_CURRENT_SOURCE_DIR}/../mrdocs.yml"
        "--output=${CMAKE_CURRENT_BINARY_DIR}/${name}-output"
        ${MRDOCS_BUILTIN_DIR_ARGS}
        "--addons-supplemental=${addons}")
endfunction ()

# A library with neither entry point, as a dependency of a plugin would be if
# it were put in the plugins directory instead of plugins/lib.
set(addons "${CMAKE_CURRENT_BINARY_DIR}/not-a-plugin-addons")
mrdocs_add_c_plugin(mrdocs-plugin-not-a-plugin "${addons}" NotAPlugin.c)
mrdocs_add_refusal(not-a-plugin
    "the plugin \".*mrdocs-plugin-not-a-plugin.*\" does not export mrdocs_plugin_abi_version"
    "${addons}")

# A library with the ABI function and no entry point.
set(addons "${CMAKE_CURRENT_BINARY_DIR}/no-init-addons")
mrdocs_add_c_plugin(mrdocs-plugin-no-init "${addons}" NoInit.c)
mrdocs_add_refusal(no-init
    "the plugin \".*mrdocs-plugin-no-init.*\" does not export mrdocs_plugin_init"
    "${addons}")

# A library with an entry point and no ABI function: a plugin that lost the
# export of `mrdocs_plugin_abi_version` is refused.
set(addons "${CMAKE_CURRENT_BINARY_DIR}/no-abi-addons")
mrdocs_add_c_plugin(mrdocs-plugin-no-abi "${addons}" NoAbiVersion.c)
mrdocs_add_refusal(no-abi
    "the plugin \".*mrdocs-plugin-no-abi.*\" does not export mrdocs_plugin_abi_version"
    "${addons}")

# A plugin that reports ABI 0, which is refused before its entry point runs.
set(addons "${CMAKE_CURRENT_BINARY_DIR}/abi-zero-addons")
mrdocs_add_c_plugin(mrdocs-plugin-abi-zero "${addons}" AbiZero.c)
mrdocs_add_refusal(abi-zero
    "plugin \".*mrdocs-plugin-abi-zero.*\" reports ABI 0, which no MrDocs provides"
    "${addons}")

# An entry point that fails with a message, and one that fails without.
set(addons "${CMAKE_CURRENT_BINARY_DIR}/init-error-addons")
mrdocs_add_c_plugin(mrdocs-plugin-init-error "${addons}" InitError.c)
mrdocs_add_refusal(init-error
    "the plugin \".*mrdocs-plugin-init-error.*\" failed: the entry point gave up"
    "${addons}")

set(addons "${CMAKE_CURRENT_BINARY_DIR}/init-no-message-addons")
mrdocs_add_c_plugin(mrdocs-plugin-init-no-message "${addons}" InitNoMessage.c)
mrdocs_add_refusal(init-no-message
    "the plugin \".*mrdocs-plugin-init-no-message.*\" failed with status plugin_error"
    "${addons}")

# The same generator id in two plugins: the one that loads second is refused.
# The libraries load in name order.
set(addons "${CMAKE_CURRENT_BINARY_DIR}/duplicate-addons")
mrdocs_add_c_plugin(mrdocs-plugin-duplicate-a "${addons}" Generator.c)
mrdocs_add_c_plugin(mrdocs-plugin-duplicate-b "${addons}" Generator.c)
foreach (target mrdocs-plugin-duplicate-a mrdocs-plugin-duplicate-b)
    target_compile_definitions(${target} PRIVATE
        "MRDOCS_TEST_GENERATOR_ID=\"duplicate\"")
endforeach ()
mrdocs_add_refusal(duplicate-generator
    "the plugin \".*mrdocs-plugin-duplicate-b.*\" failed.*generator id=\"duplicate\" already exists"
    "${addons}")

# A generator id that ships with MrDocs.
set(addons "${CMAKE_CURRENT_BINARY_DIR}/builtin-addons")
mrdocs_add_c_plugin(mrdocs-plugin-builtin-id "${addons}" Generator.c)
target_compile_definitions(mrdocs-plugin-builtin-id PRIVATE
    "MRDOCS_TEST_GENERATOR_ID=\"xml\"")
mrdocs_add_refusal(builtin-generator
    "the plugin \".*mrdocs-plugin-builtin-id.*\" failed.*generator id=\"xml\" already exists"
    "${addons}")

# A symbolic link in the plugins directory whose target is missing, as one
# left by a build tree that was moved or cleaned. It has a library name, so it
# is a plugin that cannot be loaded and the run names it. Creating a link
# needs a privilege on Windows, so the case is left out there.
if (NOT WIN32)
    set(addons "${CMAKE_CURRENT_BINARY_DIR}/dangling-link-addons")
    file(MAKE_DIRECTORY "${addons}/plugins")
    file(CREATE_LINK "no-such-directory/missing-plugin.so"
        "${addons}/plugins/mrdocs-plugin-dangling.so" SYMBOLIC)
    mrdocs_add_refusal(dangling-link
        "the plugin \".*mrdocs-plugin-dangling.so\" could not be loaded"
        "${addons}")
endif ()
