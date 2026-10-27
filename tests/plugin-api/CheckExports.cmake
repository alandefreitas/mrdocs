#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Lists the symbols the mrdocs executable exports and checks that they are the
# MrDocs API and nothing else. Every exported symbol has to demangle to a name
# in the mrdocs namespace (which includes the type information, vtables,
# thunks and guard variables of its types, and its static locals); anything
# else, such as a symbol of LLVM, Clang, Lua or JerryScript, fails the check
# and is listed. A plugin that bundles its own copy
# of one of those libraries has to bind to its own copy, which it can only do
# if the executable does not export one. The table also has to hold the type
# information of a MrDocs class, which a plugin that derives from one needs and
# which a mistake in the symbol list is likely to lose.
#
# Inputs: NM (the symbol lister), CXXFILT (c++filt or llvm-cxxfilt), EXE (the
# executable).

cmake_minimum_required(VERSION 3.13)

if (APPLE)
    set(nm_args -gU)
else ()
    set(nm_args --dynamic --defined-only)
endif ()

# nm piped into the demangler: one demangled symbol per line.
function(list_exports out_listing out_failed)
    execute_process(
        COMMAND "${NM}" ${ARGN} "${EXE}"
        COMMAND "${CXXFILT}"
        OUTPUT_VARIABLE listing
        RESULT_VARIABLE results
        ERROR_VARIABLE errors)
    set(failed "")
    foreach (result IN LISTS results)
        if (NOT result EQUAL 0)
            set(failed "${errors}")
        endif ()
    endforeach ()
    set(${out_listing} "${listing}" PARENT_SCOPE)
    set(${out_failed} "${failed}" PARENT_SCOPE)
endfunction()

# The symbol versions are what tell a symbol of another object apart (see
# below). GNU nm before 2.35 prints them only with --with-symbol-versions,
# which llvm-nm does not accept, since it prints them anyway.
set(failed "")
if (NOT APPLE)
    list_exports(listing failed ${nm_args} --with-symbol-versions)
endif ()
if (APPLE OR failed)
    list_exports(listing failed ${nm_args})
endif ()
if (failed)
    message(FATAL_ERROR "listing the exports of ${EXE} failed: ${failed}")
endif ()

string(REPLACE "\n" ";" lines "${listing}")
set(exported "")
foreach (line IN LISTS lines)
    # "<address> <type> <name>", the type being a letter. A name with an @ is
    # a symbol of another object, such as a libstdc++ data symbol the
    # executable has a copy relocation for. The exception is @@Base, which GNU
    # nm before 2.35 appends to the symbols of the executable itself.
    # The header Mach-O executables define for the dynamic loader and the
    # symbols the linker defines for the ends of the sections are not MrDocs's.
    if (line MATCHES "^[0-9a-fA-F]* +[A-Za-z] +([^@]+)(@@Base)?$")
        set(symbol "${CMAKE_MATCH_1}")
        if (NOT symbol MATCHES "^_*(mh_execute_header|init|fini|edata|end|bss_start)$")
            list(APPEND exported "${symbol}")
        endif ()
    endif ()
endforeach ()
list(LENGTH exported count)
message(STATUS "${EXE} exports ${count} symbols")
if (count EQUAL 0)
    message(FATAL_ERROR "no exported symbol found in ${EXE}: the listing could not be read")
endif ()

set(typeinfo_count 0)
set(foreign "")
foreach (name IN LISTS exported)
    if (name MATCHES "^typeinfo for mrdocs::")
        math(EXPR typeinfo_count "${typeinfo_count} + 1")
    endif ()
    # Only the name counts, not the parameters of a function in the mrdocs
    # namespace that takes a type of another library, so the text up to the
    # first parenthesis is looked at. A function template carries its return
    # type in front of the name and typeinfo, vtables and thunks a
    # description, so the namespace may follow a space.
    string(REPLACE "decltype(auto) " "" bare "${name}")
    string(REGEX REPLACE "\\(.*$" "" bare "${bare}")
    # A name the demangler could not read stays mangled, and is then judged by
    # its mangling: the namespace is the first component of the nested name.
    if (NOT bare MATCHES "(^| )mrdocs::"
            AND NOT bare MATCHES "^_?_Z(NK?[RO]?|T[ISVTWH]N|T[hvc][A-Za-z0-9_]*_NK?[RO]?|GVZ?NK?[RO]?|ZNK?[RO]?)6mrdocs")
        list(APPEND foreign "${name}")
    endif ()
endforeach ()
message(STATUS "${typeinfo_count} of them are the type information of a mrdocs type")

if (typeinfo_count EQUAL 0)
    message(SEND_ERROR "${EXE} exports no 'typeinfo for mrdocs::' symbol")
endif ()
list(LENGTH foreign foreign_count)
if (foreign_count GREATER 0)
    list(SUBLIST foreign 0 20 sample)
    string(REPLACE ";" "\n  " sample "${sample}")
    message(SEND_ERROR "${EXE} exports ${foreign_count} symbols that are not in the mrdocs namespace, for example:\n  ${sample}")
endif ()
