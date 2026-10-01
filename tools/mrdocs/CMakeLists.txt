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
    PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include/>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/src"
)

target_compile_definitions(mrdocs PRIVATE -DMRDOCS_TOOL)
# The core is linked privately so that a plugin, which links the tool, does not
# link the core as well: a second copy of a static core would give the plugin
# its own generator and transform registries.
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
# Tells tests/plugin-api whether the export table is narrowed, so that it
# checks the table only when it is.
set_property(TARGET mrdocs PROPERTY MRDOCS_NARROW_EXPORTS ${MRDOCS_EXPORTS_NARROWED})

target_include_directories(mrdocs INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/polyfill/include>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/dom/include>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/handlebars/include>")

# What a plugin needs to compile the public headers. Since the tool links
# mrdocs-core privately, so that a plugin does not link the core itself, the
# usage requirements mrdocs-core states PUBLIC do not reach a plugin and are
# repeated here: /Zc:__cplusplus for Platform.hpp, and /Zc:preprocessor for the
# variadic macros in Describe.hpp and MRDOCS_TRY, which the traditional
# preprocessor expands into something that does not compile.
target_compile_options(mrdocs PUBLIC
    "$<$<CXX_COMPILER_ID:MSVC>:/EHs;/Zc:__cplusplus;/Zc:preprocessor>")
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
    install(TARGETS mrdocs
            EXPORT mrdocs-targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
