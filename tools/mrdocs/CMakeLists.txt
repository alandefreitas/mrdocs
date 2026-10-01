#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
# Copyright (c) 2026 Gennaro Prota (gennaro.prota@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# The mrdocs command-line tool. Depends on the mrdocs-core library. The
# command line is parsed straight from argv against the reflected config
# schema, so the tool has no generated sources of its own.
file(GLOB_RECURSE TOOL_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)

add_executable(mrdocs ${TOOL_SOURCES})
target_include_directories(mrdocs
    PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
)

target_compile_definitions(mrdocs PRIVATE -DMRDOCS_TOOL)
# The tool states no usage requirements: a plugin links mrdocs::plugin, below,
# and a program that uses MrDocs as a library links mrdocs::mrdocs-core.
target_link_libraries(mrdocs PRIVATE mrdocs-core)

set_target_properties(mrdocs PROPERTIES ENABLE_EXPORTS ON)

# The executable exports the functions of <mrdocs/plugin.h> and nothing else:
# not the C++ of MrDocs, not LLVM, not the libraries MrDocs links. ENABLE_EXPORTS
# exports every symbol on Linux and macOS, so an explicit list narrows it. On
# Linux a version script does it; --exclude-libs=ALL would hide the C API too,
# since it is defined in the static mrdocs-core. On macOS it is an exported
# symbols list. Windows exports what carries MRDOCS_PLUGIN_API. A shared build
# defines the C API in the library and keeps the default. A sanitized build
# keeps the default too: clang links the sanitizer runtime statically into the
# executable, and a plugin built with the same flags binds to the runtime's
# entry points (__asan_*, __ubsan_*, the interceptors) through the executable,
# so narrowing the table would leave the plugin with undefined symbols.
#
# MRDOCS_NARROW_EXPORTS switches the narrowing. Its default is ON for the static,
# non-sanitized build and OFF otherwise. A narrowed table leaves the dynamic
# symbol table with no names for MrDocs, LLVM or Clang frames, so the fallback
# backtrace of a crash prints addresses there; configure with
# MRDOCS_NARROW_EXPORTS=OFF to get names, or symbolize the addresses against the
# executable, which is not stripped, with llvm-symbolizer or addr2line.
set(MRDOCS_SANITIZED OFF)
foreach (MRDOCS_FLAGS_VAR IN ITEMS
         CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS
         CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
         CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    if ("${${MRDOCS_FLAGS_VAR}}" MATCHES "-fsanitize")
        set(MRDOCS_SANITIZED ON)
    endif ()
endforeach ()
if (NOT MRDOCS_BUILD_SHARED AND NOT MRDOCS_SANITIZED)
    set(MRDOCS_NARROW_EXPORTS_DEFAULT ON)
else ()
    set(MRDOCS_NARROW_EXPORTS_DEFAULT OFF)
endif ()
option(MRDOCS_NARROW_EXPORTS
    "Export only the mrdocs_* functions of the plugin C API from the mrdocs executable (Linux and macOS, static builds)"
    ${MRDOCS_NARROW_EXPORTS_DEFAULT})
set(MRDOCS_EXPORTS_NARROWED OFF)
if (MRDOCS_NARROW_EXPORTS AND MRDOCS_BUILD_SHARED)
    message(STATUS "MRDOCS_NARROW_EXPORTS is ignored in a shared build: the library defines the C API")
elseif (MRDOCS_NARROW_EXPORTS)
    if (APPLE)
        target_link_options(mrdocs PRIVATE
            "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports-macos.txt")
        set_property(TARGET mrdocs APPEND PROPERTY
            LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports-macos.txt")
        set(MRDOCS_EXPORTS_NARROWED ON)
    elseif (UNIX AND NOT CYGWIN)
        target_link_options(mrdocs PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports-linux.map")
        set_property(TARGET mrdocs APPEND PROPERTY
            LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports-linux.map")
        set(MRDOCS_EXPORTS_NARROWED ON)
    endif ()
endif ()
# The Linux release links libstdc++ statically, and only the narrowed table
# keeps that copy from being exported to, and bound by, the libstdc++ of a
# plugin (utils/cmake/release-linux.cmake).
if (MRDOCS_LINUX_RELEASE AND NOT MRDOCS_EXPORTS_NARROWED)
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE needs the narrowed export table of the mrdocs "
        "executable, which keeps the statically linked libstdc++ out of the "
        "dynamic symbol table: configure with MRDOCS_NARROW_EXPORTS=ON and "
        "without a sanitizer. See utils/cmake/release-linux.cmake")
endif ()
# Tells tests/plugin-api whether the export table is narrowed, so that it
# checks the table only when it is.
set_property(TARGET mrdocs PROPERTY MRDOCS_NARROW_EXPORTS ${MRDOCS_EXPORTS_NARROWED})

# What a plugin links. A plugin compiles <mrdocs/plugin.h> or <mrdocs/plugin.hpp>
# and binds to the mrdocs_* functions of the running tool, so the include
# directory is all it needs to compile: no C++ standard beyond what the plugin
# chooses (a plugin written in C needs none), no compiler option, no library of
# MrDocs. Linux and the other ELF platforms let a module leave symbols undefined,
# and the tool provides them when it loads the module. macOS needs the linker
# told so. Windows resolves them against the import library of the executable,
# which ENABLE_EXPORTS makes the linker write and the installation ships. The
# import library names the module "mrdocs.exe", so on Windows only the tool under
# that name can load a plugin: a renamed tool or an embedding program cannot.
add_library(mrdocs-plugin INTERFACE)
add_library(mrdocs::plugin ALIAS mrdocs-plugin)
set_target_properties(mrdocs-plugin PROPERTIES EXPORT_NAME plugin)
target_include_directories(mrdocs-plugin INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
if (WIN32)
    target_link_libraries(mrdocs-plugin INTERFACE mrdocs)
elseif (APPLE)
    target_link_options(mrdocs-plugin INTERFACE
        "LINKER:-undefined,dynamic_lookup")
endif ()

# The command-line frontend uses a few LLVM utilities directly (pretty stack
# traces, host triple), so the tool links LLVM rather than reaching it only
# through mrdocs-core.
target_link_libraries(mrdocs PRIVATE mrdocs-llvm)
if (MRDOCS_CLANG)
    target_compile_options(mrdocs PRIVATE -Wno-covered-switch-default)
endif ()

# The config schema header is generated by utils/codegen; order it first.
if (TARGET mrdocs-codegen)
    add_dependencies(mrdocs mrdocs-codegen)
endif ()

# Install the tool into the shared export set (finalized by mrdocs_install()).
if (MRDOCS_INSTALL)
    install(TARGETS mrdocs mrdocs-plugin
            EXPORT mrdocs-targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
