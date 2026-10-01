#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Lists the symbols the mrdocs executable exports and checks that they are
# exactly the functions of the plugin C API: every function the header declares
# is there, and nothing else is, so no C++, LLVM or Clang symbol and no
# mrdocs_ function the header does not declare.
#
# Inputs: NM (the symbol lister: nm on macOS, readelf elsewhere), EXE (the
# executable), HEADER (plugin.h).

cmake_minimum_required(VERSION 3.13)

if (APPLE)
    set(nm_args -gU)
else ()
    # readelf prints name@VERSION (n) for a versioned dynamic symbol in every
    # binutils release, which nm --dynamic only does since binutils 2.35.
    set(nm_args --dyn-syms -W)
endif ()

execute_process(
    COMMAND "${NM}" ${nm_args} "${EXE}"
    OUTPUT_VARIABLE listing
    RESULT_VARIABLE result
    ERROR_VARIABLE errors)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${EXE}: ${errors}")
endif ()

# One symbol per line; macOS prefixes C names with an underscore.
set(exported "")
string(REPLACE "\n" ";" lines "${listing}")
foreach (line IN LISTS lines)
    set(name "")
    if (APPLE)
        if (line MATCHES "^[0-9a-fA-F]* +[A-Za-z] +([^ ]+)$")
            string(REGEX REPLACE "^_" "" name "${CMAKE_MATCH_1}")
        endif ()
    elseif (line MATCHES "^ *[0-9]+:")
        # Num: Value Size Type Bind Vis Ndx Name. Size is decimal below 100000
        # and 0x-prefixed hex above, Vis can be followed by bracketed st_other
        # notes, and the null symbol at index 0 has no name. A symbol line
        # that does not parse is an error: skipping it would let an export
        # escape the check below.
        if (line MATCHES "^ *[0-9]+: +[0-9a-fA-F]+ +(0x[0-9a-fA-F]+|[0-9]+) +[A-Za-z_]+ +([A-Z]+) +[A-Z]+( +\\[[^]]*\\])* +([A-Za-z0-9]+)( +([^ ]+))?")
            # A local symbol or an undefined one (Ndx UND, an import) is not
            # an export.
            if (NOT CMAKE_MATCH_2 STREQUAL "LOCAL" AND NOT CMAKE_MATCH_4 STREQUAL "UND")
                set(name "${CMAKE_MATCH_6}")
            endif ()
        else ()
            message(SEND_ERROR "cannot parse the symbol table line: ${line}")
        endif ()
    endif ()
    if (NOT name STREQUAL "")
        # A versioned name, as in name@GLIBC_2.2.5, belongs to another object:
        # the executable has no version nodes of its own to define one with, and
        # a copy relocation of libc or libstdc++ data (stderr, std::cout) is
        # defined in the executable and has to stay in its dynamic table.
        if (name MATCHES "@")
            continue()
        endif ()
        # The header Mach-O executables define for the dynamic loader.
        if (name STREQUAL "_mh_execute_header")
            continue()
        endif ()
        list(APPEND exported "${name}")
    endif ()
endforeach ()
list(LENGTH exported count)
message(STATUS "${EXE} exports ${count} symbols")

# The functions the header declares for plugins to call, whatever their return
# type. The entry points a plugin defines, mrdocs_plugin_*, are not declared
# with MRDOCS_PLUGIN_API here.
file(READ "${HEADER}" header)
string(REGEX MATCHALL "MRDOCS_PLUGIN_API[ \n]+[^;(#]*[ \n*](mrdocs_[a-z_0-9]+)\\(" declarations "${header}")
set(expected "")
foreach (declaration IN LISTS declarations)
    string(REGEX REPLACE ".*(mrdocs_[a-z_0-9]+)\\($" "\\1" name "${declaration}")
    list(APPEND expected "${name}")
endforeach ()
list(REMOVE_DUPLICATES expected)
list(LENGTH expected expected_count)
if (expected_count LESS 1)
    message(FATAL_ERROR "no MRDOCS_PLUGIN_API function found in ${HEADER}")
endif ()
message(STATUS "${HEADER} declares ${expected_count} functions")

foreach (name IN LISTS expected)
    if (NOT name IN_LIST exported)
        message(SEND_ERROR "${name} is declared in plugin.h but not exported")
    endif ()
endforeach ()

# Nothing else is exported: no name the header does not declare.
foreach (name IN LISTS exported)
    if (NOT name IN_LIST expected)
        message(SEND_ERROR "unexpected export: ${name}")
    endif ()
endforeach ()
