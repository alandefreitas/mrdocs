#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
# Copyright (c) 2023 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

#-------------------------------------------------
#
# Project data
#
#-------------------------------------------------
cmake_minimum_required(VERSION 3.13)
project(
    MrDocs
    VERSION 0.8.0
    DESCRIPTION "C++ Documentation Tool"
    HOMEPAGE_URL "https://github.com/cppalliance/mrdocs"
    LANGUAGES CXX C
)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
include(utils/cmake/helpers.cmake)

#-------------------------------------------------
#
# Project options
#
#-------------------------------------------------
# What to build
option(MRDOCS_MRDOCS_BUILD "Build only public-headers for documentation self-reference" ${MRDOCS_BUILD})
if (MRDOCS_MRDOCS_BUILD)
    include(docs/mrdocs-build.cmake)
    return()
endif()
option(MRDOCS_BUILD_TESTS "Build tests" ${BUILD_TESTING})
option(MRDOCS_BUILD_STRICT_TESTS "Enable costly strict tests" ON)
option(MRDOCS_BUILD_EXAMPLES "Build the examples" ${MRDOCS_BUILD_TESTS})
option(MRDOCS_BUILD_DOCS "Build documentation" OFF)
option(MRDOCS_GENERATE_REFERENCE "Generate MrDocs reference" ${MRDOCS_BUILD_DOCS})
option(MRDOCS_INSTALL "Configure install target" ON)
option(MRDOCS_PACKAGE "Build install package" ON)
if (MRDOCS_BUILD_TESTS OR MRDOCS_BUILD_EXAMPLES)
    enable_testing()
    include(CTest)
endif()

# How to build
option(MRDOCS_REQUIRE_GIT "Git is required: not being able to extract version build is an error" ON)
option(MRDOCS_BUNDLE_DEPENDENCIES "Bundle the private dependencies into the library" ON)
option(MRDOCS_BUILD_SHARED "Link shared" ${BUILD_SHARED_LIBS})
option(MRDOCS_GENERATE_ANTORA_REFERENCE "Generate MrDocs reference in Antora module pages" OFF)

# Helper variables based on options
set_ternary(MRDOCS_LINK_MODE MRDOCS_BUILD_SHARED SHARED "")
set_ternary(MRDOCS_LINK_MODE_DEFINITION MRDOCS_BUILD_SHARED MRDOCS_SHARED_LINK MRDOCS_STATIC_LINK)
set_ternary(MRDOCS_DO_BUNDLE_DEPENDENCIES "MRDOCS_BUNDLE_DEPENDENCIES AND NOT MRDOCS_BUILD_SHARED" ON OFF)
set_ternary(MRDOCS_DO_REEXPORT_DEPENDENCIES "NOT MRDOCS_DO_BUNDLE_DEPENDENCIES AND NOT MRDOCS_BUILD_SHARED" ON OFF)
set_ternary(MRDOCS_GCC   "CMAKE_CXX_COMPILER_ID STREQUAL \"GNU\"" ON OFF)
set_ternary(MRDOCS_CLANG "CMAKE_CXX_COMPILER_ID MATCHES \"Clang$\"" ON OFF)

# Two settings cover plugins.
#
# MRDOCS_PLUGIN_MODULES_SUPPORTED says whether the MrDocs built here can load
# plugins. A tool linked with -static or -static-pie cannot: a static glibc has
# no dependable way to load a shared library, and a module library cannot even
# be linked with the startup files a static link brings in. Nor can a shared
# build, where the API a plugin calls lives in mrdocs-core rather than in the
# tool. The check sees -static, -static-pie and -Wl,-static in CMAKE_C_FLAGS,
# CMAKE_CXX_FLAGS and CMAKE_EXE_LINKER_FLAGS, plain and with the DEBUG, RELEASE,
# RELWITHDEBINFO, MINSIZEREL, CMAKE_BUILD_TYPE and CMAKE_CONFIGURATION_TYPES
# suffixes; configure with
# -DMRDOCS_PLUGIN_MODULES_SUPPORTED=OFF when the flag comes from somewhere
# else, such as a toolchain file or LDFLAGS. It decides what the tool exports,
# whether mrdocs-core defines MRDOCS_WITHOUT_PLUGINS, and the package config's
# MRDOCS_BUILT_PLUGIN_MODULES.
#
# MRDOCS_BUILD_PLUGIN_MODULES says whether this build runs its own plugin tests
# and examples. It defaults to MRDOCS_PLUGIN_MODULES_SUPPORTED, and turning it
# on where plugins are not supported is an error, since the tests would run
# against a tool that cannot load them.
set(mrdocs_flag_suffixes "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
foreach (mrdocs_config IN LISTS CMAKE_CONFIGURATION_TYPES ITEMS ${CMAKE_BUILD_TYPE})
    string(TOUPPER "${mrdocs_config}" mrdocs_config)
    if (NOT mrdocs_config STREQUAL "")
        list(APPEND mrdocs_flag_suffixes _${mrdocs_config})
    endif ()
endforeach ()
list(REMOVE_DUPLICATES mrdocs_flag_suffixes)
set(MRDOCS_STATIC_LINK_FLAGS OFF)
set(MRDOCS_SANITIZED OFF)
foreach (mrdocs_suffix IN LISTS mrdocs_flag_suffixes)
    foreach (mrdocs_flag_variable IN ITEMS
            CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS)
        if ("${${mrdocs_flag_variable}${mrdocs_suffix}}" MATCHES
                "(^|[ \t\r\n;])(--?static(-pie)?|-Wl,-static)($|[ \t\r\n;])")
            set(MRDOCS_STATIC_LINK_FLAGS ON)
        endif ()
        if ("${${mrdocs_flag_variable}${mrdocs_suffix}}" MATCHES "-fsanitize=")
            set(MRDOCS_SANITIZED ON)
        endif ()
    endforeach ()
endforeach ()
if (NOT DEFINED MRDOCS_PLUGIN_MODULES_SUPPORTED)
    set_ternary(MRDOCS_PLUGIN_MODULES_SUPPORTED
        "MRDOCS_BUILD_SHARED OR MRDOCS_STATIC_LINK_FLAGS" OFF ON)
endif ()
option(MRDOCS_BUILD_PLUGIN_MODULES "Build and run the plugin tests and examples"
       ${MRDOCS_PLUGIN_MODULES_SUPPORTED})
if (MRDOCS_BUILD_PLUGIN_MODULES AND NOT MRDOCS_PLUGIN_MODULES_SUPPORTED)
    message(FATAL_ERROR
        "MRDOCS_BUILD_PLUGIN_MODULES needs a MrDocs that can load plugins, "
        "and MRDOCS_PLUGIN_MODULES_SUPPORTED is off: the flags link "
        "statically (-static, --static, -static-pie or -Wl,-static), the "
        "build is shared (MRDOCS_BUILD_SHARED), or it was set off by hand. "
        "The plugin tests would run against a tool built without plugin "
        "support. Configure with "
        "-DMRDOCS_BUILD_PLUGIN_MODULES=OFF or use a fresh build directory.")
endif ()

# A host that loads plugins exports its symbols so that a plugin resolves the
# MrDocs functions it calls against the host, and ENABLE_EXPORTS exports every
# symbol on Linux and macOS, LLVM, Clang and Lua included. Narrowing the table
# to the MrDocs API keeps a plugin that bundles its own copy of one of those
# libraries bound to its own copy; see `mrdocs_make_plugin_host`. A shared
# build keeps the default table, since the API lives in the library and not in
# the host. A sanitized build (any `-fsanitize=` in the flag variables scanned
# above) keeps it too: clang links the sanitizer runtime
# statically into the executable, and a plugin built with the same flags binds
# to the runtime's entry points (__asan_*, __ubsan_*, the interceptors) through
# the executable, so narrowing would leave the plugin with undefined symbols.
# A narrowed table leaves the dynamic symbol table with no names for LLVM or
# Clang frames, so the fallback backtrace of a crash prints addresses there;
# configure with MRDOCS_NARROW_EXPORTS=OFF to get names back.
if (MRDOCS_PLUGIN_MODULES_SUPPORTED AND NOT MRDOCS_BUILD_SHARED AND NOT MRDOCS_SANITIZED)
    set(MRDOCS_NARROW_EXPORTS_DEFAULT ON)
else ()
    set(MRDOCS_NARROW_EXPORTS_DEFAULT OFF)
endif ()
# AUTO follows the build on every configure; ON and OFF are the user's choice.
set(MRDOCS_NARROW_EXPORTS AUTO CACHE STRING
    "Export only the MrDocs API from a host that loads plugins (AUTO, ON or OFF; Linux and macOS, static builds)")
set_property(CACHE MRDOCS_NARROW_EXPORTS PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${MRDOCS_NARROW_EXPORTS}" MRDOCS_NARROW_EXPORTS_VALUE)
if (MRDOCS_NARROW_EXPORTS_VALUE STREQUAL "AUTO")
    set(MRDOCS_NARROW_EXPORTS ${MRDOCS_NARROW_EXPORTS_DEFAULT})
elseif (NOT MRDOCS_NARROW_EXPORTS_VALUE MATCHES "^(ON|OFF|TRUE|FALSE|YES|NO|Y|N|1|0)$")
    message(FATAL_ERROR "MRDOCS_NARROW_EXPORTS must be AUTO or a CMake boolean, not '${MRDOCS_NARROW_EXPORTS}'")
endif ()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# MSVC flags shared by every mrdocs target below (not installed consumers):
#  - /EHs enables C++ exception handling. The mrdocs tool, the unit tests, and the
#    examples are separate targets whose TUs include exception-using STL headers, so
#    without it MSVC raises C4530 and /WX makes it fatal; the EH model must also be
#    uniform across all TUs, so it belongs here rather than on mrdocs-core alone.
#  - /wd4100 accepts unused parameters, an accepted style across mrdocs (visitor /
#    tag_invoke / finalizer signatures) that the clang build already allows via
#    -Wno-unused-parameter; mirror it so /W4 /WX does not fail on C4100.
if (MSVC)
    add_compile_options(/EHs /wd4100)
endif()

#-------------------------------------------------
#
# Project Components
#
#-------------------------------------------------
add_subdirectory(utils)
add_subdirectory(libs)
add_subdirectory(src)
add_subdirectory(tools)
if (MRDOCS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if (MRDOCS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if (MRDOCS_BUILD_DOCS)
    add_subdirectory(docs)
endif()
if (MRDOCS_INSTALL)
    add_subdirectory(data)
    mrdocs_install()
endif()
